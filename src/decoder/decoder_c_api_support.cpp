#include "decoder/decoder_c_api_support.hpp"

#include <mutex>

#include "c_api_internal.hpp"
#include "decoder/decoder_pipeline.hpp"
#include "decoder/decoder_prefetch.hpp"
#include "input_video_probe.hpp"

namespace mkvc::decoder::capi {

bool valid_decoder_config(const mkvc_decoder_config* config) noexcept {
    return config != nullptr && config->struct_size >= sizeof(mkvc_decoder_config) &&
           config->struct_version == 1 && config->input_path_utf8 != nullptr &&
           config->input_path_utf8[0] != '\0' &&
           (config->codec == MKVC_CODEC_AUTO || config->codec == MKVC_CODEC_VP9 ||
            config->codec == MKVC_CODEC_AV1) &&
           (config->backend == MKVC_BACKEND_CPU || config->backend == MKVC_BACKEND_INTEL ||
            config->backend == MKVC_BACKEND_NVIDIA);
}

mkvc_result create_decoder(const mkvc_decoder_config& config,
                           std::unique_ptr<mkvc_decoder>& decoder, std::string& error) {
    auto handle = std::make_unique<mkvc_decoder>();
    const mkvc_result probe_result =
        probe_input_video(config.input_path_utf8, handle->video_info, error);
    if (probe_result != MKVC_OK) return probe_result;
    if (config.codec != MKVC_CODEC_AUTO && config.codec != handle->video_info.codec) {
        error = "input video codec does not match decoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    mkvc_decoder_config resolved = config;
    resolved.codec = handle->video_info.codec;
    const mkvc_result result = create_backend(*handle, resolved, error);
    if (result != MKVC_OK) return result;
    handle->capacity = config.prefetch;
    start_prefetch(*handle);
    decoder = std::move(handle);
    return MKVC_OK;
}

mkvc_result set_copy_policy(mkvc_decoder& decoder, const mkvc_copy_policy& policy,
                            std::string& error) {
    std::lock_guard<std::mutex> lock(decoder.mutex);
    if (decoder.accepted_frames != 0 || decoder.completed_frames != 0) {
        error = "copy policy must be set before the first decoder frame";
        return MKVC_ERROR_INVALID_STATE;
    }
    if ((policy.require_gpu_resident != 0 || policy.allow_cpu_copy == 0) && decoder.capacity != 0) {
        error = "GPU-resident decoding currently requires prefetch=0";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (policy.require_gpu_resident != 0 && policy.allow_cpu_copy != 0) {
        error = "require_gpu_resident conflicts with allow_cpu_copy";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    if (policy.require_gpu_resident != 0 && !decoder.intel_implementation &&
        !decoder.nvidia_implementation) {
        error = "GPU-resident decoding is unavailable for this backend";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    decoder.require_gpu_resident = policy.require_gpu_resident != 0;
    decoder.allow_gpu_copy = policy.allow_gpu_copy != 0;
    decoder.allow_cpu_copy = policy.allow_cpu_copy != 0;
    return MKVC_OK;
}

mkvc_result check_cpu_read(const mkvc_decoder& decoder, std::string& error) {
    std::lock_guard<std::mutex> lock(decoder.mutex);
    if (decoder.require_gpu_resident || !decoder.allow_cpu_copy) {
        error = "CPU frame read is prohibited by copy policy";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    return MKVC_OK;
}

mkvc_result check_gpu_read(const mkvc_decoder& decoder, std::string& error) {
    std::lock_guard<std::mutex> lock(decoder.mutex);
    if (decoder.capacity != 0) {
        error = "GPU read currently requires decoder prefetch=0";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (!decoder.intel_implementation && !decoder.nvidia_implementation) {
        error = "GPU read is not implemented for this decoder backend";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    return MKVC_OK;
}

void snapshot_metrics(const mkvc_decoder& decoder, mkvc_pipeline_metrics& metrics) {
    mkvc_pipeline_metrics snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    snapshot.struct_version = 1;
    std::lock_guard<std::mutex> lock(decoder.mutex);
    snapshot.accepted_frames = decoder.accepted_frames;
    snapshot.completed_frames = decoder.completed_frames;
    snapshot.queue_wait_ns = decoder.queue_wait_ns;
    snapshot.backend_time_ns = decoder.backend_time_ns;
    snapshot.queue_capacity = static_cast<uint32_t>(decoder.capacity);
    snapshot.peak_queue_depth = decoder.peak_queue_depth;
    snapshot.hardware_pending_peak = decoder.hardware_pending_peak;
    snapshot.copy_path =
        decoder.completed_frames == 0 ? MKVC_COPY_PATH_UNKNOWN : MKVC_COPY_PATH_CPU;
    if (decoder.gpu_path_exercised) snapshot.copy_path = MKVC_COPY_PATH_ZERO_COPY;
    metrics = snapshot;
}

}  // namespace mkvc::decoder::capi
