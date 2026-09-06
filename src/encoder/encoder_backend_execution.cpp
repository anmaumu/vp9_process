#include "encoder/encoder_backend_execution.hpp"

#include <algorithm>

namespace mkvc::encoder {

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

mkvc_result write_cpu_sync(EncoderSession::Impl& impl, const mkvc_frame_view& frame,
                           std::string& error) {
    const auto started = std::chrono::steady_clock::now();
    const mkvc_result result = backend_write(impl, frame, error);
    std::lock_guard<std::mutex> lock(impl.mutex);
    impl.backend_time_ns += elapsed_ns(started);
    impl.hardware_pending_peak =
        std::max(impl.hardware_pending_peak, backend_hardware_pending(impl));
    if (result == MKVC_OK) {
        ++impl.accepted_frames;
        ++impl.completed_frames;
        observe_copy_path(impl, MKVC_COPY_PATH_CPU);
    }
    return result;
}

mkvc_result write_gpu_sync_locked(EncoderSession::Impl& impl,
                                  const std::shared_ptr<gpu::GpuFrameCore>& frame,
                                  std::string& error) {
    const auto started = std::chrono::steady_clock::now();
    const mkvc_result result = impl.backend->write_gpu(frame, error);
    impl.backend_time_ns += elapsed_ns(started);
    impl.hardware_pending_peak =
        std::max(impl.hardware_pending_peak, backend_hardware_pending(impl));
    if (result == MKVC_OK) {
        ++impl.accepted_frames;
        ++impl.completed_frames;
        observe_copy_path(impl, MKVC_COPY_PATH_ZERO_COPY);
    }
    return result;
}

mkvc_result flush_sync(EncoderSession::Impl& impl, std::string& error) {
    const auto started = std::chrono::steady_clock::now();
    const mkvc_result result = backend_flush(impl, error);
    std::lock_guard<std::mutex> lock(impl.mutex);
    impl.backend_time_ns += elapsed_ns(started);
    impl.hardware_pending_peak =
        std::max(impl.hardware_pending_peak, backend_hardware_pending(impl));
    return result;
}

mkvc_result close_sync(EncoderSession::Impl& impl, std::string& error) {
    const auto started = std::chrono::steady_clock::now();
    const mkvc_result result = backend_close(impl, error);
    std::lock_guard<std::mutex> lock(impl.mutex);
    impl.backend_time_ns += elapsed_ns(started);
    impl.hardware_pending_peak =
        std::max(impl.hardware_pending_peak, backend_hardware_pending(impl));
    return result;
}

}  // namespace mkvc::encoder
