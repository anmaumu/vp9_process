#include "encoder_session.hpp"
#include "cpu_av1_encoder.hpp"
#include "intel_webm_encoder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace mkvc {
namespace {

struct OwnedFrame {
    uint32_t pixel_format = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts = -1;
    std::array<std::vector<uint8_t>, 3> planes;
    std::array<int32_t, 3> strides{};

    mkvc_frame_view view() const {
        mkvc_frame_view result{};
        result.struct_size = sizeof(result);
        result.struct_version = 1;
        result.pixel_format = pixel_format;
        result.width = width;
        result.height = height;
        result.pts = pts;
        for (size_t index = 0; index < planes.size(); ++index) {
            result.planes[index] =
                planes[index].empty() ? nullptr : planes[index].data();
            result.strides[index] = strides[index];
        }
        return result;
    }
};

bool copy_plane(const uint8_t* source, int32_t source_stride,
                uint32_t row_bytes, uint32_t rows,
                std::vector<uint8_t>& destination, int32_t& destination_stride) {
    if (source == nullptr || source_stride < static_cast<int32_t>(row_bytes)) {
        return false;
    }
    destination_stride = static_cast<int32_t>(row_bytes);
    destination.resize(static_cast<size_t>(row_bytes) * rows);
    for (uint32_t row = 0; row < rows; ++row) {
        std::memcpy(destination.data() + static_cast<size_t>(row) * row_bytes,
                    source + static_cast<size_t>(row) * source_stride, row_bytes);
    }
    return true;
}

mkvc_result own_frame(const mkvc_frame_view& source, OwnedFrame& destination,
                      std::string& error) {
    destination.pixel_format = source.pixel_format;
    destination.width = source.width;
    destination.height = source.height;
    destination.pts = source.pts;
    bool valid = false;
    switch (source.pixel_format) {
        case MKVC_PIXEL_FORMAT_I420:
            valid = copy_plane(source.planes[0], source.strides[0], source.width,
                               source.height, destination.planes[0],
                               destination.strides[0]) &&
                    copy_plane(source.planes[1], source.strides[1],
                               source.width / 2, source.height / 2,
                               destination.planes[1], destination.strides[1]) &&
                    copy_plane(source.planes[2], source.strides[2],
                               source.width / 2, source.height / 2,
                               destination.planes[2], destination.strides[2]);
            break;
        case MKVC_PIXEL_FORMAT_NV12:
            valid = copy_plane(source.planes[0], source.strides[0], source.width,
                               source.height, destination.planes[0],
                               destination.strides[0]) &&
                    copy_plane(source.planes[1], source.strides[1], source.width,
                               source.height / 2, destination.planes[1],
                               destination.strides[1]);
            break;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
            valid = copy_plane(source.planes[0], source.strides[0],
                               source.width * 3, source.height,
                               destination.planes[0], destination.strides[0]);
            break;
        case MKVC_PIXEL_FORMAT_BGRA32:
            valid = copy_plane(source.planes[0], source.strides[0],
                               source.width * 4, source.height,
                               destination.planes[0], destination.strides[0]);
            break;
        default:
            error = "unsupported asynchronous input pixel format";
            return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (!valid) {
        error = "asynchronous input has an invalid plane or stride";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    return MKVC_OK;
}

}  // namespace

struct EncoderSession::Impl {
    enum class ItemType { kFrame, kFlush };
    struct Item {
        ItemType type = ItemType::kFrame;
        std::unique_ptr<OwnedFrame> frame;
        uint64_t flush_token = 0;
    };

    std::unique_ptr<CpuVp9Encoder> encoder;
    std::unique_ptr<CpuAv1Encoder> av1_encoder;
    std::unique_ptr<IntelWebmEncoder> intel_encoder;
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
#if defined(MKVC_ENABLE_TEST_HOOKS)
    uint64_t test_fail_after = std::numeric_limits<uint64_t>::max();
#endif
};

namespace {

mkvc_result backend_write(EncoderSession::Impl& impl,
                          const mkvc_frame_view& frame, std::string& error) {
    if (impl.intel_encoder) return impl.intel_encoder->write(frame, error);
    return impl.encoder ? impl.encoder->write(frame, error)
                        : impl.av1_encoder->write(frame, error);
}

mkvc_result backend_flush(EncoderSession::Impl& impl, std::string& error) {
    if (impl.intel_encoder) return impl.intel_encoder->flush(error);
    return impl.encoder ? impl.encoder->flush(error)
                        : impl.av1_encoder->flush(error);
}

mkvc_result backend_close(EncoderSession::Impl& impl, std::string& error) {
    if (impl.intel_encoder) return impl.intel_encoder->close(error);
    return impl.encoder ? impl.encoder->close(error)
                        : impl.av1_encoder->close(error);
}

uint32_t backend_hardware_pending(const EncoderSession::Impl& impl) {
    return impl.intel_encoder ? impl.intel_encoder->max_pending_observed() : 0;
}

uint64_t elapsed_ns(std::chrono::steady_clock::time_point started) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
}

void encoder_worker(EncoderSession::Impl* impl) noexcept {
    try {
        while (true) {
            EncoderSession::Impl::Item item;
            bool inject_failure = false;
            {
                std::unique_lock<std::mutex> lock(impl->mutex);
                impl->has_items.wait(lock, [impl] {
                    return impl->close_requested || !impl->queue.empty();
                });
                if (impl->queue.empty() && impl->close_requested) {
                    break;
                }
                item = std::move(impl->queue.front());
                impl->queue.pop_front();
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
                if (inject_failure) {
                    result = MKVC_ERROR_IO;
                    error = "injected asynchronous backend device loss";
                } else {
                    const mkvc_frame_view view = item.frame->view();
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
                impl->hardware_pending_peak = std::max(
                    impl->hardware_pending_peak, hardware_pending);
                if (item.frame) {
                    impl->free_frames.push_back(std::move(item.frame));
                    impl->has_space.notify_all();
                }
                if (result != MKVC_OK) {
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
                    ++impl->completed_frames;
                }
            }
        }

        std::string error;
        const auto close_started = std::chrono::steady_clock::now();
        const mkvc_result close_result = backend_close(*impl, error);
        const uint64_t close_elapsed = elapsed_ns(close_started);
        const uint32_t hardware_pending = backend_hardware_pending(*impl);
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->backend_time_ns += close_elapsed;
        impl->hardware_pending_peak = std::max(
            impl->hardware_pending_peak, hardware_pending);
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
        impl->failed = true;
        impl->closed = true;
        impl->accepting = false;
        impl->terminal_result = MKVC_ERROR_INTERNAL;
        impl->terminal_error = exception.what();
        impl->state_changed.notify_all();
        impl->has_space.notify_all();
    } catch (...) {
        std::lock_guard<std::mutex> lock(impl->mutex);
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

EncoderSession::EncoderSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

std::unique_ptr<EncoderSession> EncoderSession::create(
    const mkvc_encoder_config& config, std::string& error) {
    auto impl = std::make_unique<Impl>();
    if (config.backend == MKVC_BACKEND_INTEL) {
        impl->intel_encoder = IntelWebmEncoder::create(config, error);
        if (!impl->intel_encoder) return nullptr;
    } else if (config.codec == MKVC_CODEC_VP9) {
        impl->encoder = CpuVp9Encoder::create(config, error);
        if (!impl->encoder) return nullptr;
    } else if (config.codec == MKVC_CODEC_AV1) {
        impl->av1_encoder = CpuAv1Encoder::create(config, error);
        if (!impl->av1_encoder) return nullptr;
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
#endif
    for (size_t index = 0; index < impl->capacity; ++index) {
        impl->free_frames.push_back(std::make_unique<OwnedFrame>());
    }
    auto session =
        std::unique_ptr<EncoderSession>(new EncoderSession(std::move(impl)));
    if (session->impl_->capacity > 0) {
        session->impl_->worker =
            std::thread(encoder_worker, session->impl_.get());
    }
    return session;
}

EncoderSession::~EncoderSession() {
    std::string ignored;
    close(ignored);
}

mkvc_result EncoderSession::write(const mkvc_frame_view& frame, bool block,
                                  std::string& error) {
    if (impl_->capacity == 0) {
        const auto started = std::chrono::steady_clock::now();
        const mkvc_result result = backend_write(*impl_, frame, error);
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->backend_time_ns += elapsed_ns(started);
        impl_->hardware_pending_peak = std::max(
            impl_->hardware_pending_peak, backend_hardware_pending(*impl_));
        if (result == MKVC_OK) {
            ++impl_->accepted_frames;
            ++impl_->completed_frames;
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
            error = "encoder is closing or closed";
            return MKVC_ERROR_INVALID_STATE;
        }
    }
    if (frame.width != impl_->width || frame.height != impl_->height) {
        error = "frame dimensions do not match encoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    std::unique_ptr<OwnedFrame> owned;
    {
        std::unique_lock<std::mutex> lock(impl_->mutex);
        if (!block && (impl_->queue.size() >= impl_->capacity ||
                       impl_->free_frames.empty())) {
            ++impl_->rejected_frames;
            return MKVC_WOULD_BLOCK;
        }
        if (block) {
            const auto wait_started = std::chrono::steady_clock::now();
            impl_->has_space.wait(lock, [this] {
                return (impl_->queue.size() < impl_->capacity &&
                        !impl_->free_frames.empty()) ||
                       !impl_->accepting || impl_->failed;
            });
            impl_->queue_wait_ns += elapsed_ns(wait_started);
        }
        if (impl_->failed) {
            error = impl_->terminal_error;
            return impl_->terminal_result;
        }
        if (!impl_->accepting) {
            error = "encoder is closing or closed";
            return MKVC_ERROR_INVALID_STATE;
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
            return impl_->queue.size() < impl_->capacity ||
                   !impl_->accepting || impl_->failed;
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
        error = "encoder is closing or closed";
        return MKVC_ERROR_INVALID_STATE;
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
    impl_->peak_queue_depth = std::max<uint32_t>(
        impl_->peak_queue_depth, static_cast<uint32_t>(impl_->queue.size()));
    impl_->has_items.notify_one();
    return MKVC_OK;
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
    metrics.copy_path = impl_->completed_frames == 0
        ? MKVC_COPY_PATH_UNKNOWN : MKVC_COPY_PATH_CPU;
}

mkvc_result EncoderSession::flush(std::string& error) {
    if (impl_->capacity == 0) {
        const auto started = std::chrono::steady_clock::now();
        const mkvc_result result = backend_flush(*impl_, error);
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->backend_time_ns += elapsed_ns(started);
        impl_->hardware_pending_peak = std::max(
            impl_->hardware_pending_peak, backend_hardware_pending(*impl_));
        return result;
    }
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->has_space.wait(lock, [this] {
        return impl_->queue.size() < impl_->capacity ||
               !impl_->accepting || impl_->failed;
    });
    if (impl_->failed) {
        error = impl_->terminal_error;
        return impl_->terminal_result;
    }
    if (!impl_->accepting) {
        error = "encoder is closing or closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    const uint64_t token = ++impl_->next_flush_token;
    Impl::Item item;
    item.type = Impl::ItemType::kFlush;
    item.flush_token = token;
    impl_->queue.push_back(std::move(item));
    impl_->has_items.notify_one();
    impl_->state_changed.wait(lock, [this, token] {
        return impl_->completed_flush_token >= token || impl_->failed;
    });
    if (impl_->failed) {
        error = impl_->terminal_error;
        return impl_->terminal_result;
    }
    return MKVC_OK;
}

mkvc_result EncoderSession::close(std::string& error) {
    if (impl_->capacity == 0) {
        const auto started = std::chrono::steady_clock::now();
        const mkvc_result result = backend_close(*impl_, error);
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->backend_time_ns += elapsed_ns(started);
        impl_->hardware_pending_peak = std::max(
            impl_->hardware_pending_peak, backend_hardware_pending(*impl_));
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
