#include "encoder_session.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include "cpu_av1_encoder.hpp"
#include "encoder/cpu_frame_copy.hpp"
#include "intel_webm_encoder.hpp"
#include "nvidia_webm_encoder.hpp"

namespace mkvc {
namespace {

using encoder::own_frame;
using encoder::OwnedFrame;
using encoder::validate_borrowed_frame;

}  // namespace

/** @brief Type-erased codec/container backend owned by one encoder session. */
class EncoderBackend {
   public:
    virtual ~EncoderBackend() = default;
    virtual mkvc_result write(const mkvc_frame_view& frame, std::string& error) = 0;
    virtual mkvc_result flush(std::string& error) = 0;
    virtual mkvc_result close(std::string& error) = 0;
    virtual bool supports_gpu_frames() const noexcept = 0;
    virtual mkvc_result write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                                  std::string& error) = 0;
    virtual uint32_t hardware_pending() const noexcept = 0;
};

/**
 * @brief Adapt a concrete CPU or GPU encoder to the session backend contract.
 * @tparam Encoder Concrete codec/container encoder type.
 * @tparam SupportsGpu Whether the concrete type accepts GPU-frame leases.
 */
template <typename Encoder, bool SupportsGpu>
class EncoderBackendAdapter final : public EncoderBackend {
   public:
    explicit EncoderBackendAdapter(std::unique_ptr<Encoder> encoder)
        : encoder_(std::move(encoder)) {}

    mkvc_result write(const mkvc_frame_view& frame, std::string& error) override {
        return encoder_->write(frame, error);
    }

    mkvc_result flush(std::string& error) override { return encoder_->flush(error); }

    mkvc_result close(std::string& error) override { return encoder_->close(error); }

    bool supports_gpu_frames() const noexcept override { return SupportsGpu; }

    mkvc_result write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                          std::string& error) override {
        if constexpr (SupportsGpu) {
            return encoder_->write_gpu(frame, error);
        } else {
            (void)frame;
            error = "GPU frame is not compatible with this encoder backend";
            return MKVC_ERROR_NOT_SUPPORTED;
        }
    }

    uint32_t hardware_pending() const noexcept override {
        if constexpr (SupportsGpu) {
            return encoder_->max_pending_observed();
        } else {
            return 0;
        }
    }

   private:
    std::unique_ptr<Encoder> encoder_;
};

struct EncoderSession::Impl {
    enum class ItemType { kFrame, kFlush };
    struct Item {
        ItemType type = ItemType::kFrame;
        std::unique_ptr<OwnedFrame> frame;
        mkvc_frame_view borrowed{};
        std::shared_ptr<CpuSubmission> submission;
        uint64_t flush_token = 0;
    };

    std::unique_ptr<EncoderBackend> backend;
    uint32_t width = 0;
    uint32_t height = 0;
    size_t capacity = 0;
    mutable std::mutex mutex;
    std::condition_variable has_items;
    std::condition_variable has_space;
    std::condition_variable state_changed;
    std::deque<Item> queue;
    std::deque<std::unique_ptr<OwnedFrame>> free_frames;
    std::thread worker;
    bool accepting = true;
    bool close_requested = false;
    bool closed = false;
    bool failed = false;
    bool canceled = false;
    mkvc_result terminal_result = MKVC_OK;
    std::string terminal_error;
    uint64_t next_flush_token = 0;
    uint64_t completed_flush_token = 0;
    uint64_t accepted_frames = 0;
    uint64_t completed_frames = 0;
    uint64_t rejected_frames = 0;
    uint64_t queue_wait_ns = 0;
    uint64_t backend_time_ns = 0;
    uint32_t peak_queue_depth = 0;
    uint32_t hardware_pending_peak = 0;
    uint32_t copy_path = MKVC_COPY_PATH_UNKNOWN;
    bool require_gpu_resident = false;
    bool allow_gpu_copy = true;
    bool allow_cpu_copy = true;
#if defined(MKVC_ENABLE_TEST_HOOKS)
    uint64_t test_fail_after = std::numeric_limits<uint64_t>::max();
    uint32_t test_delay_ms = 0;
#endif
};

namespace {

void fail_queued_submissions(EncoderSession::Impl& impl, mkvc_result result,
                             const std::string& error) noexcept {
    for (auto& queued : impl.queue) {
        if (queued.submission) queued.submission->complete(result, error);
    }
}

mkvc_result backend_write(EncoderSession::Impl& impl, const mkvc_frame_view& frame,
                          std::string& error) {
    return impl.backend->write(frame, error);
}

mkvc_result backend_flush(EncoderSession::Impl& impl, std::string& error) {
    return impl.backend->flush(error);
}

mkvc_result backend_close(EncoderSession::Impl& impl, std::string& error) {
    return impl.backend->close(error);
}

uint32_t backend_hardware_pending(const EncoderSession::Impl& impl) {
    return impl.backend->hardware_pending();
}

uint64_t elapsed_ns(std::chrono::steady_clock::time_point started) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count());
}

void observe_copy_path(EncoderSession::Impl& impl, uint32_t path) {
    if (impl.copy_path == MKVC_COPY_PATH_UNKNOWN)
        impl.copy_path = path;
    else if (impl.copy_path != path)
        impl.copy_path = MKVC_COPY_PATH_MIXED;
}

void encoder_worker(EncoderSession::Impl* impl) noexcept {
    std::shared_ptr<CpuSubmission> active_submission;
    try {
        while (true) {
            EncoderSession::Impl::Item item;
            bool inject_failure = false;
            {
                std::unique_lock<std::mutex> lock(impl->mutex);
                impl->has_items.wait(
                    lock, [impl] { return impl->close_requested || !impl->queue.empty(); });
                if (impl->queue.empty() && impl->close_requested) {
                    break;
                }
                item = std::move(impl->queue.front());
                impl->queue.pop_front();
                active_submission = item.submission;
#if defined(MKVC_ENABLE_TEST_HOOKS)
                inject_failure = item.type == EncoderSession::Impl::ItemType::kFrame &&
                                 impl->completed_frames >= impl->test_fail_after;
#endif
                impl->has_space.notify_all();
            }

            std::string error;
            mkvc_result result = MKVC_OK;
            const auto backend_started = std::chrono::steady_clock::now();
            if (item.type == EncoderSession::Impl::ItemType::kFrame) {
#if defined(MKVC_ENABLE_TEST_HOOKS)
                if (impl->test_delay_ms != 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(impl->test_delay_ms));
                }
#endif
                if (inject_failure) {
                    result = MKVC_ERROR_IO;
                    error = "injected asynchronous backend device loss";
                } else {
                    const mkvc_frame_view view =
                        item.submission ? item.borrowed : item.frame->view();
                    result = backend_write(*impl, view, error);
                }
            } else {
                result = backend_flush(*impl, error);
            }
            const uint64_t backend_elapsed = elapsed_ns(backend_started);
            const uint32_t hardware_pending = backend_hardware_pending(*impl);
            {
                std::lock_guard<std::mutex> lock(impl->mutex);
                impl->backend_time_ns += backend_elapsed;
                impl->hardware_pending_peak =
                    std::max(impl->hardware_pending_peak, hardware_pending);
                if (item.frame) {
                    impl->free_frames.push_back(std::move(item.frame));
                    impl->has_space.notify_all();
                }
                if (result != MKVC_OK) {
                    if (item.submission) {
                        item.submission->complete(result, error);
                    }
                    fail_queued_submissions(*impl, result, error);
                    impl->failed = true;
                    impl->accepting = false;
                    impl->terminal_result = result;
                    impl->terminal_error = std::move(error);
                    impl->queue.clear();
                    impl->state_changed.notify_all();
                    impl->has_space.notify_all();
                    break;
                }
                if (item.type == EncoderSession::Impl::ItemType::kFlush) {
                    impl->completed_flush_token = item.flush_token;
                    impl->state_changed.notify_all();
                } else {
                    if (item.submission) {
                        item.submission->complete(MKVC_OK, {});
                    }
                    ++impl->completed_frames;
                    observe_copy_path(*impl, MKVC_COPY_PATH_CPU);
                }
                active_submission.reset();
            }
        }

        std::string error;
        const auto close_started = std::chrono::steady_clock::now();
        const mkvc_result close_result = backend_close(*impl, error);
        const uint64_t close_elapsed = elapsed_ns(close_started);
        const uint32_t hardware_pending = backend_hardware_pending(*impl);
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->backend_time_ns += close_elapsed;
        impl->hardware_pending_peak = std::max(impl->hardware_pending_peak, hardware_pending);
        if (!impl->failed && close_result != MKVC_OK) {
            impl->failed = true;
            impl->terminal_result = close_result;
            impl->terminal_error = std::move(error);
        }
        impl->closed = true;
        impl->accepting = false;
        impl->state_changed.notify_all();
        impl->has_space.notify_all();
    } catch (const std::exception& exception) {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (active_submission) {
            active_submission->complete(MKVC_ERROR_INTERNAL, exception.what());
        }
        fail_queued_submissions(*impl, MKVC_ERROR_INTERNAL, exception.what());
        impl->failed = true;
        impl->closed = true;
        impl->accepting = false;
        impl->terminal_result = MKVC_ERROR_INTERNAL;
        impl->terminal_error = exception.what();
        impl->state_changed.notify_all();
        impl->has_space.notify_all();
    } catch (...) {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (active_submission) {
            active_submission->complete(MKVC_ERROR_INTERNAL,
                                        "unknown asynchronous encoder failure");
        }
        fail_queued_submissions(*impl, MKVC_ERROR_INTERNAL, "unknown asynchronous encoder failure");
        impl->failed = true;
        impl->closed = true;
        impl->accepting = false;
        impl->terminal_result = MKVC_ERROR_INTERNAL;
        impl->terminal_error = "unknown asynchronous encoder failure";
        impl->state_changed.notify_all();
        impl->has_space.notify_all();
    }
}

}  // namespace

EncoderSession::EncoderSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<EncoderSession> EncoderSession::create(const mkvc_encoder_config& config,
                                                       std::string& error) {
    auto impl = std::make_unique<Impl>();
    if (config.backend == MKVC_BACKEND_INTEL) {
        auto encoder = IntelWebmEncoder::create(config, error);
        if (!encoder) return nullptr;
        impl->backend =
            std::make_unique<EncoderBackendAdapter<IntelWebmEncoder, true>>(std::move(encoder));
    } else if (config.backend == MKVC_BACKEND_NVIDIA) {
        auto encoder = NvidiaWebmEncoder::create(config, error);
        if (!encoder) return nullptr;
        impl->backend =
            std::make_unique<EncoderBackendAdapter<NvidiaWebmEncoder, true>>(std::move(encoder));
    } else if (config.codec == MKVC_CODEC_VP9) {
        auto encoder = CpuVp9Encoder::create(config, error);
        if (!encoder) return nullptr;
        impl->backend =
            std::make_unique<EncoderBackendAdapter<CpuVp9Encoder, false>>(std::move(encoder));
    } else if (config.codec == MKVC_CODEC_AV1) {
        auto encoder = CpuAv1Encoder::create(config, error);
        if (!encoder) return nullptr;
        impl->backend =
            std::make_unique<EncoderBackendAdapter<CpuAv1Encoder, false>>(std::move(encoder));
    } else {
        error = "unsupported encoder backend or codec";
        return nullptr;
    }
    impl->width = config.width;
    impl->height = config.height;
    impl->capacity = config.queue_size;
#if defined(MKVC_ENABLE_TEST_HOOKS)
    if (const char* value = std::getenv("MKVC_TEST_ENCODER_FAIL_AFTER")) {
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(value, &end, 10);
        if (end != value && *end == '\0') impl->test_fail_after = parsed;
    }
    if (const char* value = std::getenv("MKVC_TEST_ENCODER_DELAY_MS")) {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end != value && *end == '\0' && parsed <= std::numeric_limits<uint32_t>::max()) {
            impl->test_delay_ms = static_cast<uint32_t>(parsed);
        }
    }
#endif
    for (size_t index = 0; index < impl->capacity; ++index) {
        impl->free_frames.push_back(std::make_unique<OwnedFrame>());
    }
    auto session = std::unique_ptr<EncoderSession>(new EncoderSession(std::move(impl)));
    if (session->impl_->capacity > 0) {
        session->impl_->worker = std::thread(encoder_worker, session->impl_.get());
    }
    return session;
}

EncoderSession::~EncoderSession() {
    std::string ignored;
    close(ignored);
}

mkvc_result EncoderSession::write(const mkvc_frame_view& frame, bool block, std::string& error) {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->require_gpu_resident || !impl_->allow_cpu_copy) {
            error = "CPU frame submission is prohibited by copy policy";
            return MKVC_ERROR_NOT_SUPPORTED;
        }
        if (impl_->failed) {
            error = impl_->terminal_error;
            return impl_->terminal_result;
        }
        if (impl_->canceled) {
            error = "encoder was cancelled";
            return MKVC_ERROR_CANCELLED;
        }
        if (!impl_->accepting) {
            error = impl_->canceled ? "encoder was cancelled" : "encoder is closing or closed";
            return impl_->canceled ? MKVC_ERROR_CANCELLED : MKVC_ERROR_INVALID_STATE;
        }
    }
    if (impl_->capacity == 0) {
        const auto started = std::chrono::steady_clock::now();
        const mkvc_result result = backend_write(*impl_, frame, error);
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->backend_time_ns += elapsed_ns(started);
        impl_->hardware_pending_peak =
            std::max(impl_->hardware_pending_peak, backend_hardware_pending(*impl_));
        if (result == MKVC_OK) {
            ++impl_->accepted_frames;
            ++impl_->completed_frames;
            observe_copy_path(*impl_, MKVC_COPY_PATH_CPU);
        }
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->failed) {
            error = impl_->terminal_error;
            return impl_->terminal_result;
        }
        if (!impl_->accepting) {
            error = impl_->canceled ? "encoder was cancelled" : "encoder is closing or closed";
            return impl_->canceled ? MKVC_ERROR_CANCELLED : MKVC_ERROR_INVALID_STATE;
        }
    }
    if (frame.width != impl_->width || frame.height != impl_->height) {
        error = "frame dimensions do not match encoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    std::unique_ptr<OwnedFrame> owned;
    {
        std::unique_lock<std::mutex> lock(impl_->mutex);
        if (!block && (impl_->queue.size() >= impl_->capacity || impl_->free_frames.empty())) {
            ++impl_->rejected_frames;
            return MKVC_WOULD_BLOCK;
        }
        if (block) {
            const auto wait_started = std::chrono::steady_clock::now();
            impl_->has_space.wait(lock, [this] {
                return (impl_->queue.size() < impl_->capacity && !impl_->free_frames.empty()) ||
                       !impl_->accepting || impl_->failed;
            });
            impl_->queue_wait_ns += elapsed_ns(wait_started);
        }
        if (impl_->failed) {
            error = impl_->terminal_error;
            return impl_->terminal_result;
        }
        if (!impl_->accepting) {
            error = impl_->canceled ? "encoder was cancelled" : "encoder is closing or closed";
            return impl_->canceled ? MKVC_ERROR_CANCELLED : MKVC_ERROR_INVALID_STATE;
        }
        if (impl_->queue.size() >= impl_->capacity || impl_->free_frames.empty()) {
            ++impl_->rejected_frames;
            return MKVC_WOULD_BLOCK;
        }
        owned = std::move(impl_->free_frames.front());
        impl_->free_frames.pop_front();
    }
    mkvc_result result = own_frame(frame, *owned, error);
    if (result != MKVC_OK) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->free_frames.push_back(std::move(owned));
        impl_->has_space.notify_all();
        return result;
    }
    std::unique_lock<std::mutex> lock(impl_->mutex);
    if (block) {
        const auto wait_started = std::chrono::steady_clock::now();
        impl_->has_space.wait(lock, [this] {
            return impl_->queue.size() < impl_->capacity || !impl_->accepting || impl_->failed;
        });
        impl_->queue_wait_ns += elapsed_ns(wait_started);
    }
    if (impl_->failed) {
        impl_->free_frames.push_back(std::move(owned));
        error = impl_->terminal_error;
        return impl_->terminal_result;
    }
    if (!impl_->accepting) {
        impl_->free_frames.push_back(std::move(owned));
        error = impl_->canceled ? "encoder was cancelled" : "encoder is closing or closed";
        return impl_->canceled ? MKVC_ERROR_CANCELLED : MKVC_ERROR_INVALID_STATE;
    }
    if (impl_->queue.size() >= impl_->capacity) {
        impl_->free_frames.push_back(std::move(owned));
        impl_->has_space.notify_all();
        ++impl_->rejected_frames;
        return MKVC_WOULD_BLOCK;
    }
    Impl::Item item;
    item.frame = std::move(owned);
    impl_->queue.push_back(std::move(item));
    ++impl_->accepted_frames;
    impl_->peak_queue_depth =
        std::max<uint32_t>(impl_->peak_queue_depth, static_cast<uint32_t>(impl_->queue.size()));
    impl_->has_items.notify_one();
    return MKVC_OK;
}

mkvc_result EncoderSession::write_borrowed(const mkvc_frame_view& frame, std::string& error) {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->capacity != 0) {
            error = "borrowed CPU writes require queue_size=0";
            return MKVC_ERROR_NOT_SUPPORTED;
        }
        if (impl_->failed) {
            error = impl_->terminal_error;
            return impl_->terminal_result;
        }
        if (!impl_->accepting) {
            error = impl_->canceled ? "encoder was cancelled" : "encoder is closing or closed";
            return impl_->canceled ? MKVC_ERROR_CANCELLED : MKVC_ERROR_INVALID_STATE;
        }
    }
    return write(frame, true, error);
}

mkvc_result EncoderSession::submit_borrowed(const mkvc_frame_view& frame,
                                            std::shared_ptr<CpuSubmission>& submission,
                                            std::string& error) {
    if (frame.width != impl_->width || frame.height != impl_->height) {
        error = "frame dimensions do not match encoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const mkvc_result validation = validate_borrowed_frame(frame, error);
    if (validation != MKVC_OK) return validation;
    auto state = std::make_shared<CpuSubmission>();
    std::unique_lock<std::mutex> lock(impl_->mutex);
    if (impl_->capacity == 0) {
        error = "async borrowed submission requires queue_size greater than zero";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (impl_->require_gpu_resident || !impl_->allow_cpu_copy) {
        error = "CPU frame submission is prohibited by copy policy";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    const auto wait_started = std::chrono::steady_clock::now();
    impl_->has_space.wait(lock, [this] {
        return impl_->queue.size() < impl_->capacity || !impl_->accepting || impl_->failed;
    });
    impl_->queue_wait_ns += elapsed_ns(wait_started);
    if (impl_->failed) {
        error = impl_->terminal_error;
        return impl_->terminal_result;
    }
    if (!impl_->accepting) {
        error = impl_->canceled ? "encoder was cancelled" : "encoder is closing or closed";
        return impl_->canceled ? MKVC_ERROR_CANCELLED : MKVC_ERROR_INVALID_STATE;
    }
    Impl::Item item;
    item.borrowed = frame;
    item.submission = state;
    impl_->queue.push_back(std::move(item));
    ++impl_->accepted_frames;
    impl_->peak_queue_depth =
        std::max<uint32_t>(impl_->peak_queue_depth, static_cast<uint32_t>(impl_->queue.size()));
    submission = std::move(state);
    impl_->has_items.notify_one();
    return MKVC_OK;
}

mkvc_result EncoderSession::set_copy_policy(const mkvc_copy_policy& policy, std::string& error) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->accepted_frames != 0 || impl_->completed_frames != 0) {
        error = "copy policy must be set before the first encoder frame";
        return MKVC_ERROR_INVALID_STATE;
    }
    if ((policy.require_gpu_resident != 0 || policy.allow_cpu_copy == 0) && impl_->capacity != 0) {
        error = "GPU-resident encoding currently requires queue_size=0";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (policy.require_gpu_resident != 0 && !impl_->backend->supports_gpu_frames()) {
        error = "GPU-resident encoding is unavailable for this backend";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (policy.require_gpu_resident != 0 && policy.allow_cpu_copy != 0) {
        error = "require_gpu_resident conflicts with allow_cpu_copy";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    impl_->require_gpu_resident = policy.require_gpu_resident != 0;
    impl_->allow_gpu_copy = policy.allow_gpu_copy != 0;
    impl_->allow_cpu_copy = policy.allow_cpu_copy != 0;
    return MKVC_OK;
}

mkvc_result EncoderSession::write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                                      std::string& error) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->capacity != 0) {
        error = "GPU frame submission currently requires queue_size=0";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (impl_->failed) {
        error = impl_->terminal_error;
        return impl_->terminal_result;
    }
    if (!impl_->accepting || impl_->closed) {
        error = impl_->canceled ? "encoder was cancelled" : "encoder is closing or closed";
        return impl_->canceled ? MKVC_ERROR_CANCELLED : MKVC_ERROR_INVALID_STATE;
    }
    if (!impl_->backend->supports_gpu_frames()) {
        error = "GPU frame is not compatible with this encoder backend";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    const auto started = std::chrono::steady_clock::now();
    const mkvc_result result = impl_->backend->write_gpu(frame, error);
    impl_->backend_time_ns += elapsed_ns(started);
    impl_->hardware_pending_peak =
        std::max(impl_->hardware_pending_peak, backend_hardware_pending(*impl_));
    if (result == MKVC_OK) {
        ++impl_->accepted_frames;
        ++impl_->completed_frames;
        observe_copy_path(*impl_, MKVC_COPY_PATH_ZERO_COPY);
    }
    return result;
}

void EncoderSession::get_metrics(mkvc_pipeline_metrics& metrics) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    metrics.accepted_frames = impl_->accepted_frames;
    metrics.completed_frames = impl_->completed_frames;
    metrics.rejected_frames = impl_->rejected_frames;
    metrics.queue_wait_ns = impl_->queue_wait_ns;
    metrics.backend_time_ns = impl_->backend_time_ns;
    metrics.queue_capacity = static_cast<uint32_t>(impl_->capacity);
    metrics.peak_queue_depth = impl_->peak_queue_depth;
    metrics.hardware_pending_peak = impl_->hardware_pending_peak;
    metrics.copy_path = impl_->copy_path;
}

mkvc_result EncoderSession::flush(std::string& error) {
    if (impl_->capacity == 0) {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (impl_->canceled) {
                error = "encoder was cancelled";
                return MKVC_ERROR_CANCELLED;
            }
            if (impl_->failed) {
                error = impl_->terminal_error;
                return impl_->terminal_result;
            }
        }
        const auto started = std::chrono::steady_clock::now();
        const mkvc_result result = backend_flush(*impl_, error);
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->backend_time_ns += elapsed_ns(started);
        impl_->hardware_pending_peak =
            std::max(impl_->hardware_pending_peak, backend_hardware_pending(*impl_));
        return result;
    }
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->has_space.wait(lock, [this] {
        return impl_->queue.size() < impl_->capacity || !impl_->accepting || impl_->failed;
    });
    if (impl_->failed) {
        error = impl_->terminal_error;
        return impl_->terminal_result;
    }
    if (!impl_->accepting) {
        error = impl_->canceled ? "encoder was cancelled" : "encoder is closing or closed";
        return impl_->canceled ? MKVC_ERROR_CANCELLED : MKVC_ERROR_INVALID_STATE;
    }
    const uint64_t token = ++impl_->next_flush_token;
    Impl::Item item;
    item.type = Impl::ItemType::kFlush;
    item.flush_token = token;
    impl_->queue.push_back(std::move(item));
    impl_->has_items.notify_one();
    impl_->state_changed.wait(lock, [this, token] {
        return impl_->completed_flush_token >= token || impl_->failed || impl_->canceled;
    });
    if (impl_->failed) {
        error = impl_->terminal_error;
        return impl_->terminal_result;
    }
    if (impl_->canceled) {
        error = "encoder was cancelled";
        return MKVC_ERROR_CANCELLED;
    }
    return MKVC_OK;
}

mkvc_result EncoderSession::cancel(std::string& error) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->closed || impl_->canceled) return MKVC_OK;
    if (impl_->failed) {
        error = impl_->terminal_error;
        return impl_->terminal_result;
    }
    impl_->canceled = true;
    impl_->accepting = false;
    if (impl_->capacity != 0) impl_->close_requested = true;
    for (auto& item : impl_->queue) {
        if (item.submission) {
            item.submission->complete(MKVC_ERROR_CANCELLED, "encoder submission was cancelled");
        }
        if (item.frame) impl_->free_frames.push_back(std::move(item.frame));
    }
    impl_->queue.clear();
    impl_->has_items.notify_all();
    impl_->has_space.notify_all();
    impl_->state_changed.notify_all();
    return MKVC_OK;
}

mkvc_result EncoderSession::close(std::string& error) {
    if (impl_->capacity == 0) {
        const auto started = std::chrono::steady_clock::now();
        const mkvc_result result = backend_close(*impl_, error);
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->backend_time_ns += elapsed_ns(started);
        impl_->hardware_pending_peak =
            std::max(impl_->hardware_pending_peak, backend_hardware_pending(*impl_));
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->close_requested) {
            impl_->accepting = false;
            impl_->close_requested = true;
            impl_->has_items.notify_all();
            impl_->has_space.notify_all();
        }
    }
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->failed) {
        error = impl_->terminal_error;
        return impl_->terminal_result;
    }
    return MKVC_OK;
}

}  // namespace mkvc
