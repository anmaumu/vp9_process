#include "encoder_session.hpp"

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include "encoder/cpu_frame_copy.hpp"
#include "encoder/encoder_backend_execution.hpp"
#include "encoder/encoder_backend_factory.hpp"
#include "encoder/encoder_queue_control.hpp"
#include "encoder/encoder_session_state.hpp"
#include "encoder/encoder_worker.hpp"

namespace mkvc {
namespace {

using encoder::close_sync;
using encoder::create_backend;
using encoder::enqueue_borrowed;
using encoder::enqueue_owned;
using encoder::flush_and_wait;
using encoder::flush_sync;
using encoder::OwnedFrame;
using encoder::run_encoder_worker;
using encoder::write_cpu_sync;
using encoder::write_gpu_sync_locked;

}  // namespace

EncoderSession::EncoderSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<EncoderSession> EncoderSession::create(const mkvc_encoder_config& config,
                                                       std::string& error) {
    auto impl = std::make_unique<Impl>();
    impl->backend = create_backend(config, error);
    if (!impl->backend) return nullptr;
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
        session->impl_->worker = std::thread(run_encoder_worker, session->impl_.get());
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
        return write_cpu_sync(*impl_, frame, error);
    }
    return enqueue_owned(*impl_, frame, block, error);
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
    return enqueue_borrowed(*impl_, frame, submission, error);
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
    return write_gpu_sync_locked(*impl_, frame, error);
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
        return flush_sync(*impl_, error);
    }
    return flush_and_wait(*impl_, error);
}

mkvc_result EncoderSession::cancel(std::string& error) { return encoder::cancel(*impl_, error); }

mkvc_result EncoderSession::close(std::string& error) {
    if (impl_->capacity == 0) {
        return close_sync(*impl_, error);
    }
    return encoder::close_and_join(*impl_, error);
}

}  // namespace mkvc
