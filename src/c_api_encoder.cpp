/**
 * @file c_api_encoder.cpp
 * @brief C ABI adapters for encoder creation, submission, and completion.
 */
#include <memory>
#include <string>
#include <utility>

#include "c_api_internal.hpp"
#include "c_api_validation.hpp"
#include "encoder/encoder_c_api_support.hpp"
#include "gpu/gpu_frame.hpp"

namespace {
#define last_error mkvc_last_error
using mkvc::capi::fail;
using mkvc::capi::guard;
}  // namespace

extern "C" {

mkvc_result mkvc_encoder_create(const mkvc_encoder_config* config, mkvc_encoder** out_encoder) {
    last_error.clear();
    if (out_encoder != nullptr) {
        *out_encoder = nullptr;
    }
    if (out_encoder == nullptr || !mkvc::encoder::capi::valid_encoder_config(config)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder config");
    }
    return guard("unknown encoder creation failure", [&] {
        std::string error;
        std::unique_ptr<mkvc::EncoderSession> implementation;
        const mkvc_result result =
            mkvc::encoder::capi::create_encoder_session(*config, implementation, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        auto handle = std::make_unique<mkvc_encoder>();
        handle->implementation = std::move(implementation);
        *out_encoder = handle.release();
        return MKVC_OK;
    });
}

mkvc_result mkvc_encoder_set_copy_policy(mkvc_encoder* encoder, const mkvc_copy_policy* policy) {
    last_error.clear();
    if (encoder == nullptr || !mkvc::capi::valid_copy_policy(policy)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder copy policy");
    }
    return guard("unknown encoder copy policy failure", [&] {
        std::string error;
        const mkvc_result result = encoder->implementation->set_copy_policy(*policy, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    });
}

mkvc_result mkvc_encoder_write_frame(mkvc_encoder* encoder, const mkvc_frame_view* frame) {
    last_error.clear();
    if (encoder == nullptr || !mkvc::encoder::capi::valid_frame_view(frame)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or frame view");
    }
    return guard("unknown frame write failure", [&] {
        std::string error;
        const mkvc_result result = encoder->implementation->write(*frame, true, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    });
}

mkvc_result mkvc_encoder_write_frame_borrowed(mkvc_encoder* encoder, const mkvc_frame_view* frame) {
    last_error.clear();
    if (encoder == nullptr || !mkvc::encoder::capi::valid_frame_view(frame)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or borrowed frame view");
    }
    return guard("unknown borrowed frame write failure", [&] {
        std::string error;
        const mkvc_result result = encoder->implementation->write_borrowed(*frame, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    });
}

mkvc_result mkvc_encoder_submit_frame_borrowed(mkvc_encoder* encoder, const mkvc_frame_view* frame,
                                               mkvc_submission** out_submission) {
    last_error.clear();
    if (encoder == nullptr || out_submission == nullptr ||
        !mkvc::encoder::capi::valid_frame_view(frame)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT,
                    "invalid encoder, borrowed frame, or submission output");
    }
    *out_submission = nullptr;
    return guard("unknown borrowed submission failure", [&] {
        auto handle = std::make_unique<mkvc_submission>();
        std::shared_ptr<mkvc::CpuSubmission> state;
        std::string error;
        const mkvc_result result = encoder->implementation->submit_borrowed(*frame, state, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        handle->implementation = std::move(state);
        *out_submission = handle.release();
        return MKVC_OK;
    });
}

mkvc_result mkvc_encoder_write_gpu_frame(mkvc_encoder* encoder, const mkvc_gpu_frame* frame) {
    last_error.clear();
    if (encoder == nullptr || frame == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or GPU frame");
    }
    return guard("unknown GPU frame write failure", [&] {
        auto core = mkvc::gpu::get_core(frame);
        if (!core) return fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
        std::string error;
        const mkvc_result result = encoder->implementation->write_gpu(core, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    });
}

mkvc_result mkvc_encoder_try_write_frame(mkvc_encoder* encoder, const mkvc_frame_view* frame) {
    last_error.clear();
    if (encoder == nullptr || !mkvc::encoder::capi::valid_frame_view(frame)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or frame view");
    }
    return guard("unknown nonblocking frame write failure", [&] {
        std::string error;
        const mkvc_result result = encoder->implementation->write(*frame, false, error);
        if (result == MKVC_OK || result == MKVC_WOULD_BLOCK) {
            return result;
        }
        return fail(result, std::move(error));
    });
}

mkvc_result mkvc_encoder_flush(mkvc_encoder* encoder) {
    last_error.clear();
    if (encoder == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "encoder is null");
    }
    return guard("unknown encoder flush failure", [&] {
        std::string error;
        const mkvc_result result = encoder->implementation->flush(error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    });
}

mkvc_result mkvc_encoder_cancel(mkvc_encoder* encoder) {
    last_error.clear();
    if (encoder == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "encoder is null");
    }
    return guard("unknown encoder cancel failure", [&] {
        std::string error;
        const mkvc_result result = encoder->implementation->cancel(error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    });
}

mkvc_result mkvc_encoder_close(mkvc_encoder* encoder) {
    last_error.clear();
    if (encoder == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "encoder is null");
    }
    return guard("unknown encoder close failure", [&] {
        std::string error;
        const mkvc_result result = encoder->implementation->close(error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    });
}

mkvc_result mkvc_encoder_get_metrics(const mkvc_encoder* encoder,
                                     mkvc_pipeline_metrics* out_metrics) {
    last_error.clear();
    if (encoder == nullptr || !mkvc::capi::valid_metrics_output(out_metrics)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder metrics output");
    }
    return guard("unknown encoder metrics failure", [&] {
        mkvc_pipeline_metrics metrics{};
        metrics.struct_size = sizeof(metrics);
        metrics.struct_version = 1;
        encoder->implementation->get_metrics(metrics);
        *out_metrics = metrics;
        return MKVC_OK;
    });
}

void mkvc_encoder_destroy(mkvc_encoder* encoder) {
    try {
        delete encoder;
    } catch (...) {
    }
}

}  // extern "C"
