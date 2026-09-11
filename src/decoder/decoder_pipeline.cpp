#include "decoder/decoder_pipeline.hpp"

#include <algorithm>
#include <chrono>

#include "backend_registry.hpp"
#include "c_api_internal.hpp"
#include "decoder/decoder_backend_io.hpp"
#include "decoder/decoder_prefetch.hpp"
#include "decoder/decoder_sync_read.hpp"

namespace mkvc::decoder {

mkvc_result create_backend(mkvc_decoder& decoder, const mkvc_decoder_config& config,
                           std::string& error) {
    if (config.backend == MKVC_BACKEND_INTEL ||
        config.backend == MKVC_BACKEND_NVIDIA) {
        const auto& capabilities = backend_capabilities();
        const bool available =
            std::any_of(capabilities.begin(), capabilities.end(), [&config](const auto& item) {
                return item.backend == config.backend && item.codec == config.codec &&
                       item.can_decode != 0;
            });
        if (!available) {
            error = "requested hardware decode capability is unavailable";
            return MKVC_ERROR_NOT_SUPPORTED;
        }
    }
    if (config.backend == MKVC_BACKEND_INTEL) {
        decoder.intel_implementation = IntelWebmDecoder::create(config, error);
        return decoder.intel_implementation ? MKVC_OK : MKVC_ERROR_CODEC;
    }
    if (config.backend == MKVC_BACKEND_NVIDIA) {
        decoder.nvidia_implementation = NvidiaWebmDecoder::create(config, error);
        return decoder.nvidia_implementation ? MKVC_OK : MKVC_ERROR_CODEC;
    }
    if (config.codec == MKVC_CODEC_VP9) {
        decoder.implementation = CpuVp9Decoder::create(config, error);
        return decoder.implementation ? MKVC_OK : MKVC_ERROR_CODEC;
    }
    decoder.av1_implementation = CpuAv1Decoder::create(config, error);
    return decoder.av1_implementation ? MKVC_OK : MKVC_ERROR_CODEC;
}

mkvc_result read_cpu(mkvc_decoder& decoder, std::unique_ptr<DecodedFrame>& frame,
                     std::string& error) {
    frame.reset();
    return decoder.capacity > 0 ? read_prefetched(decoder, frame, error)
                                : read_cpu_sync(decoder, frame, error);
}

mkvc_result read_gpu(mkvc_decoder& decoder, mkvc_gpu_frame** frame, std::string& error) {
    return read_gpu_sync(decoder, frame, error);
}

mkvc_result close(mkvc_decoder& decoder, std::string& error) {
    const auto started = std::chrono::steady_clock::now();
    stop_prefetch(decoder);
    const mkvc_result result = close_backend(decoder, error);
    const auto elapsed = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    std::lock_guard<std::mutex> lock(decoder.mutex);
    ++decoder.stage_metrics.close_calls;
    decoder.stage_metrics.close_time_ns += elapsed;
    return result;
}

}  // namespace mkvc::decoder
