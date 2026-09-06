/**
 * @file c_api_decoder.cpp
 * @brief C ABI adapters for decoder creation, reads, copy policy, and metrics.
 */
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "c_api_internal.hpp"
#include "decoder/decoder_pipeline.hpp"

namespace {
#define last_error mkvc_last_error
using mkvc::capi::fail;
}  // namespace

extern "C" {

mkvc_result mkvc_decoder_create(const mkvc_decoder_config* config, mkvc_decoder** out_decoder) {
    last_error.clear();
    if (out_decoder != nullptr) *out_decoder = nullptr;
    if (config == nullptr || out_decoder == nullptr ||
        config->struct_size < sizeof(mkvc_decoder_config) || config->struct_version != 1 ||
        config->input_path_utf8 == nullptr || config->input_path_utf8[0] == '\0' ||
        (config->codec != MKVC_CODEC_VP9 && config->codec != MKVC_CODEC_AV1) ||
        (config->backend != MKVC_BACKEND_CPU && config->backend != MKVC_BACKEND_INTEL &&
         config->backend != MKVC_BACKEND_NVIDIA)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder config");
    }
    try {
        std::string error;
        auto handle = std::make_unique<mkvc_decoder>();
        const mkvc_result result = mkvc::decoder::create_backend(*handle, *config, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        handle->capacity = config->prefetch;
        mkvc::decoder::start_prefetch(*handle);
        *out_decoder = handle.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown decoder creation failure");
    }
}

mkvc_result mkvc_decoder_set_copy_policy(mkvc_decoder* decoder, const mkvc_copy_policy* policy) {
    last_error.clear();
    if (decoder == nullptr || policy == nullptr || policy->struct_size < sizeof(*policy) ||
        policy->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder copy policy");
    }
    std::lock_guard<std::mutex> lock(decoder->mutex);
    if (decoder->accepted_frames != 0 || decoder->completed_frames != 0) {
        return fail(MKVC_ERROR_INVALID_STATE,
                    "copy policy must be set before the first decoder frame");
    }
    if ((policy->require_gpu_resident != 0 || policy->allow_cpu_copy == 0) &&
        decoder->capacity != 0) {
        return fail(MKVC_ERROR_NOT_SUPPORTED,
                    "GPU-resident decoding currently requires prefetch=0");
    }
    if (policy->require_gpu_resident != 0 && policy->allow_cpu_copy != 0) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT,
                    "require_gpu_resident conflicts with allow_cpu_copy");
    }
    if (policy->require_gpu_resident != 0 && !decoder->intel_implementation &&
        !decoder->nvidia_implementation) {
        return fail(MKVC_ERROR_NOT_SUPPORTED,
                    "GPU-resident decoding is unavailable for this backend");
    }
    decoder->require_gpu_resident = policy->require_gpu_resident != 0;
    decoder->allow_gpu_copy = policy->allow_gpu_copy != 0;
    decoder->allow_cpu_copy = policy->allow_cpu_copy != 0;
    return MKVC_OK;
}

mkvc_result mkvc_decoder_read(mkvc_decoder* decoder, mkvc_frame** out_frame) {
    last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (decoder == nullptr || out_frame == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder or frame output");
    }
    {
        std::lock_guard<std::mutex> lock(decoder->mutex);
        if (decoder->require_gpu_resident || !decoder->allow_cpu_copy) {
            return fail(MKVC_ERROR_NOT_SUPPORTED, "CPU frame read is prohibited by copy policy");
        }
    }
    try {
        std::unique_ptr<mkvc::DecodedFrame> decoded;
        std::string error;
        const mkvc_result result = mkvc::decoder::read_cpu(*decoder, decoded, error);
        if (result == MKVC_END_OF_STREAM) return result;
        if (result != MKVC_OK) return fail(result, std::move(error));
        auto frame = std::make_unique<mkvc_frame>();
        frame->implementation = std::move(decoded);
        *out_frame = frame.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown decoder read failure");
    }
}

mkvc_result mkvc_decoder_read_gpu(mkvc_decoder* decoder, mkvc_gpu_frame** out_frame) {
    last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (decoder == nullptr || out_frame == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder or GPU frame output");
    }
    if (decoder->capacity != 0) {
        return fail(MKVC_ERROR_NOT_SUPPORTED, "GPU read currently requires decoder prefetch=0");
    }
    if (!decoder->intel_implementation && !decoder->nvidia_implementation) {
        return fail(MKVC_ERROR_NOT_SUPPORTED,
                    "GPU read is not implemented for this decoder backend");
    }
    try {
        std::string error;
        const mkvc_result result = mkvc::decoder::read_gpu(*decoder, out_frame, error);
        return result == MKVC_OK || result == MKVC_END_OF_STREAM ? result
                                                                 : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown GPU decoder read failure");
    }
}

mkvc_result mkvc_decoder_close(mkvc_decoder* decoder) {
    last_error.clear();
    if (decoder == nullptr) return fail(MKVC_ERROR_INVALID_ARGUMENT, "decoder is null");
    try {
        std::string error;
        const mkvc_result result = mkvc::decoder::close(*decoder, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown decoder close failure");
    }
}

mkvc_result mkvc_decoder_get_metrics(const mkvc_decoder* decoder,
                                     mkvc_pipeline_metrics* out_metrics) {
    last_error.clear();
    if (decoder == nullptr || out_metrics == nullptr ||
        out_metrics->struct_size < sizeof(mkvc_pipeline_metrics) ||
        out_metrics->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder metrics output");
    }
    try {
        mkvc_pipeline_metrics metrics{};
        metrics.struct_size = sizeof(metrics);
        metrics.struct_version = 1;
        std::lock_guard<std::mutex> lock(decoder->mutex);
        metrics.accepted_frames = decoder->accepted_frames;
        metrics.completed_frames = decoder->completed_frames;
        metrics.queue_wait_ns = decoder->queue_wait_ns;
        metrics.backend_time_ns = decoder->backend_time_ns;
        metrics.queue_capacity = static_cast<uint32_t>(decoder->capacity);
        metrics.peak_queue_depth = decoder->peak_queue_depth;
        metrics.hardware_pending_peak = decoder->hardware_pending_peak;
        metrics.copy_path =
            decoder->completed_frames == 0 ? MKVC_COPY_PATH_UNKNOWN : MKVC_COPY_PATH_CPU;
        if (decoder->gpu_path_exercised) metrics.copy_path = MKVC_COPY_PATH_ZERO_COPY;
        *out_metrics = metrics;
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown decoder metrics failure");
    }
}

void mkvc_decoder_destroy(mkvc_decoder* decoder) {
    try {
        if (decoder != nullptr) {
            std::string ignored;
            (void)mkvc::decoder::close(*decoder, ignored);
        }
        delete decoder;
    } catch (...) {
    }
}

}  // extern "C"
