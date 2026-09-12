#include "decoder/decoder_sync_read.hpp"

#include <algorithm>
#include <chrono>

#include "c_api_internal.hpp"
#include "decoder/decoder_backend_io.hpp"
#include "pipeline_component_metrics.hpp"

namespace mkvc::decoder {
namespace {

uint64_t elapsed_ns(std::chrono::steady_clock::time_point started) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count());
}

}  // namespace

mkvc_result read_cpu_sync(mkvc_decoder& decoder, std::unique_ptr<DecodedFrame>& frame,
                          std::string& error) {
    const auto backend_started = std::chrono::steady_clock::now();
    const mkvc_result result = read_backend(decoder, frame, error);
    const uint64_t backend_elapsed = elapsed_ns(backend_started);
    const uint32_t pending = hardware_pending(decoder);
    {
        std::lock_guard<std::mutex> lock(decoder.mutex);
        decoder.backend_time_ns += backend_elapsed;
        ++decoder.stage_metrics.frame_calls;
        decoder.stage_metrics.frame_time_ns += backend_elapsed;
        ++decoder.stage_metrics.sync_frame_calls;
        decoder.stage_metrics.sync_frame_time_ns += backend_elapsed;
        decoder.hardware_pending_peak = std::max(decoder.hardware_pending_peak, pending);
        if (result == MKVC_OK) {
            ++decoder.accepted_frames;
            ++decoder.completed_frames;
        }
    }
    return result;
}

mkvc_result read_gpu_sync(mkvc_decoder& decoder, mkvc_gpu_frame** frame, std::string& error) {
    const auto started = std::chrono::steady_clock::now();
    mkvc_result result = MKVC_OK;
    {
        ActiveComponentMetrics active(*decoder.component_metrics);
        ScopedComponentTimer timer(PipelineComponent::kCodec);
        result = decoder.intel_implementation
                     ? decoder.intel_implementation->read_gpu(frame, error)
                     : decoder.nvidia_implementation->read_gpu(frame, error);
    }
    {
        std::lock_guard<std::mutex> lock(decoder.mutex);
        const uint64_t elapsed = elapsed_ns(started);
        decoder.backend_time_ns += elapsed;
        ++decoder.stage_metrics.frame_calls;
        decoder.stage_metrics.frame_time_ns += elapsed;
        ++decoder.stage_metrics.sync_frame_calls;
        decoder.stage_metrics.sync_frame_time_ns += elapsed;
        if (result == MKVC_OK) {
            ++decoder.accepted_frames;
            ++decoder.completed_frames;
            decoder.gpu_path_exercised = true;
        }
    }
    return result;
}

}  // namespace mkvc::decoder
