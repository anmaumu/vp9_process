/**
 * @file c_api_decoder.cpp
 * @brief C ABI adapters for decoder creation, reads, copy policy, and metrics.
 */
#include <memory>
#include <string>
#include <utility>

#include "c_api_internal.hpp"
#include "c_api_validation.hpp"
#include "decoder/decoder_c_api_support.hpp"
#include "decoder/decoder_pipeline.hpp"

namespace {
#define last_error mkvc_last_error
using mkvc::capi::fail;
using mkvc::capi::guard;
}  // namespace

extern "C" {

mkvc_result mkvc_decoder_create(const mkvc_decoder_config* config, mkvc_decoder** out_decoder) {
    last_error.clear();
    if (out_decoder != nullptr) *out_decoder = nullptr;
    if (out_decoder == nullptr || !mkvc::decoder::capi::valid_decoder_config(config)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder config");
    }
    return guard("unknown decoder creation failure", [&] {
        std::string error;
        std::unique_ptr<mkvc_decoder> handle;
        const mkvc_result result = mkvc::decoder::capi::create_decoder(*config, handle, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        *out_decoder = handle.release();
        return MKVC_OK;
    });
}

mkvc_result mkvc_decoder_set_copy_policy(mkvc_decoder* decoder, const mkvc_copy_policy* policy) {
    last_error.clear();
    if (decoder == nullptr || !mkvc::capi::valid_copy_policy(policy)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder copy policy");
    }
    return guard("unknown decoder copy policy failure", [&] {
        std::string error;
        const mkvc_result result = mkvc::decoder::capi::set_copy_policy(*decoder, *policy, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    });
}

mkvc_result mkvc_decoder_read(mkvc_decoder* decoder, mkvc_frame** out_frame) {
    last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (decoder == nullptr || out_frame == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder or frame output");
    }
    return guard("unknown decoder read failure", [&] {
        std::string error;
        const mkvc_result policy_result = mkvc::decoder::capi::check_cpu_read(*decoder, error);
        if (policy_result != MKVC_OK) return fail(policy_result, std::move(error));
        std::unique_ptr<mkvc::DecodedFrame> decoded;
        const mkvc_result result = mkvc::decoder::read_cpu(*decoder, decoded, error);
        if (result == MKVC_END_OF_STREAM) return result;
        if (result != MKVC_OK) return fail(result, std::move(error));
        auto frame = std::make_unique<mkvc_frame>();
        frame->implementation = std::move(decoded);
        frame->component_metrics = decoder->component_metrics;
        *out_frame = frame.release();
        return MKVC_OK;
    });
}

mkvc_result mkvc_decoder_read_gpu(mkvc_decoder* decoder, mkvc_gpu_frame** out_frame) {
    last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (decoder == nullptr || out_frame == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder or GPU frame output");
    }
    return guard("unknown GPU decoder read failure", [&] {
        std::string error;
        const mkvc_result capability_result = mkvc::decoder::capi::check_gpu_read(*decoder, error);
        if (capability_result != MKVC_OK) return fail(capability_result, std::move(error));
        const mkvc_result result = mkvc::decoder::read_gpu(*decoder, out_frame, error);
        return result == MKVC_OK || result == MKVC_END_OF_STREAM ? result
                                                                 : fail(result, std::move(error));
    });
}

mkvc_result mkvc_decoder_close(mkvc_decoder* decoder) {
    last_error.clear();
    if (decoder == nullptr) return fail(MKVC_ERROR_INVALID_ARGUMENT, "decoder is null");
    return guard("unknown decoder close failure", [&] {
        std::string error;
        const mkvc_result result = mkvc::decoder::close(*decoder, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    });
}

mkvc_result mkvc_decoder_get_metrics(const mkvc_decoder* decoder,
                                     mkvc_pipeline_metrics* out_metrics) {
    last_error.clear();
    if (decoder == nullptr || !mkvc::capi::valid_metrics_output(out_metrics)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder metrics output");
    }
    return guard("unknown decoder metrics failure", [&] {
        mkvc_pipeline_metrics metrics;
        mkvc::decoder::capi::snapshot_metrics(*decoder, metrics);
        *out_metrics = metrics;
        return MKVC_OK;
    });
}

mkvc_result mkvc_decoder_get_stage_metrics(const mkvc_decoder* decoder,
                                           mkvc_pipeline_stage_metrics* out_metrics) {
    last_error.clear();
    if (decoder == nullptr || !mkvc::capi::valid_stage_metrics_output(out_metrics))
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder stage-metrics output");
    std::lock_guard<std::mutex> lock(decoder->mutex);
    *out_metrics = decoder->stage_metrics;
    out_metrics->struct_size = sizeof(*out_metrics);
    out_metrics->struct_version = 1;
    return MKVC_OK;
}

mkvc_result mkvc_decoder_get_component_metrics(const mkvc_decoder* decoder,
                                               mkvc_pipeline_component_metrics* out_metrics) {
    last_error.clear();
    if (decoder == nullptr || !mkvc::capi::valid_component_metrics_output(out_metrics))
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder component-metrics output");
    *out_metrics = decoder->component_metrics->snapshot();
    return MKVC_OK;
}

mkvc_result mkvc_decoder_get_info(const mkvc_decoder* decoder, mkvc_video_info* out_info) {
    last_error.clear();
    if (decoder == nullptr || out_info == nullptr || out_info->struct_size < sizeof(*out_info) ||
        out_info->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder video-info output");
    }
    *out_info = decoder->video_info;
    return MKVC_OK;
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
