/**
 * @file c_api_encoder.cpp
 * @brief C ABI adapters for encoder creation, submission, and completion.
 */
#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "backend_registry.hpp"
#include "c_api_internal.hpp"
#include "gpu/gpu_frame.hpp"

namespace {
#define last_error mkvc_last_error
using mkvc::capi::fail;
}  // namespace

extern "C" {

mkvc_result mkvc_encoder_create(const mkvc_encoder_config* config, mkvc_encoder** out_encoder) {
    last_error.clear();
    if (out_encoder != nullptr) {
        *out_encoder = nullptr;
    }
    if (config == nullptr || out_encoder == nullptr ||
        config->struct_size < sizeof(mkvc_encoder_config) || config->struct_version != 1 ||
        config->output_path_utf8 == nullptr || config->output_path_utf8[0] == '\0' ||
        (config->codec != MKVC_CODEC_VP9 && config->codec != MKVC_CODEC_AV1) ||
        (config->backend != MKVC_BACKEND_CPU && config->backend != MKVC_BACKEND_INTEL &&
         config->backend != MKVC_BACKEND_NVIDIA) ||
        config->width == 0 || config->height == 0 || (config->width & 1u) != 0 ||
        (config->height & 1u) != 0 || config->fps_num == 0 || config->fps_den == 0 ||
        config->quality > 63 ||
        config->width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max() / 4) ||
        config->height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder config");
    }
    try {
        if (config->backend == MKVC_BACKEND_NVIDIA) {
            const auto& capabilities = mkvc::backend_capabilities();
            const bool available =
                std::any_of(capabilities.begin(), capabilities.end(), [config](const auto& item) {
                    return item.backend == MKVC_BACKEND_NVIDIA && item.codec == config->codec &&
                           item.can_encode != 0;
                });
            if (!available) {
                return fail(MKVC_ERROR_NOT_SUPPORTED,
                            "requested NVIDIA encode capability is unavailable");
            }
        }
        std::string error;
        auto implementation = mkvc::EncoderSession::create(*config, error);
        if (!implementation) {
            return fail(MKVC_ERROR_CODEC, std::move(error));
        }
        auto handle = std::make_unique<mkvc_encoder>();
        handle->implementation = std::move(implementation);
        *out_encoder = handle.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown encoder creation failure");
    }
}

mkvc_result mkvc_encoder_set_copy_policy(mkvc_encoder* encoder, const mkvc_copy_policy* policy) {
    last_error.clear();
    if (encoder == nullptr || policy == nullptr || policy->struct_size < sizeof(*policy) ||
        policy->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder copy policy");
    }
    std::string error;
    const mkvc_result result = encoder->implementation->set_copy_policy(*policy, error);
    return result == MKVC_OK ? result : fail(result, std::move(error));
}

mkvc_result mkvc_encoder_write_frame(mkvc_encoder* encoder, const mkvc_frame_view* frame) {
    last_error.clear();
    if (encoder == nullptr || frame == nullptr || frame->struct_size < sizeof(mkvc_frame_view) ||
        frame->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or frame view");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->write(*frame, true, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown frame write failure");
    }
}

mkvc_result mkvc_encoder_write_frame_borrowed(mkvc_encoder* encoder, const mkvc_frame_view* frame) {
    last_error.clear();
    if (encoder == nullptr || frame == nullptr || frame->struct_size < sizeof(mkvc_frame_view) ||
        frame->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or borrowed frame view");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->write_borrowed(*frame, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown borrowed frame write failure");
    }
}

mkvc_result mkvc_encoder_submit_frame_borrowed(mkvc_encoder* encoder, const mkvc_frame_view* frame,
                                               mkvc_submission** out_submission) {
    last_error.clear();
    if (encoder == nullptr || frame == nullptr || out_submission == nullptr ||
        frame->struct_size < sizeof(mkvc_frame_view) || frame->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT,
                    "invalid encoder, borrowed frame, or submission output");
    }
    *out_submission = nullptr;
    try {
        auto handle = std::make_unique<mkvc_submission>();
        std::shared_ptr<mkvc::CpuSubmission> state;
        std::string error;
        const mkvc_result result = encoder->implementation->submit_borrowed(*frame, state, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        handle->implementation = std::move(state);
        *out_submission = handle.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown borrowed submission failure");
    }
}

mkvc_result mkvc_encoder_write_gpu_frame(mkvc_encoder* encoder, const mkvc_gpu_frame* frame) {
    last_error.clear();
    if (encoder == nullptr || frame == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or GPU frame");
    }
    try {
        auto core = mkvc::gpu::get_core(frame);
        if (!core) return fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
        std::string error;
        const mkvc_result result = encoder->implementation->write_gpu(core, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown GPU frame write failure");
    }
}

mkvc_result mkvc_encoder_try_write_frame(mkvc_encoder* encoder, const mkvc_frame_view* frame) {
    last_error.clear();
    if (encoder == nullptr || frame == nullptr || frame->struct_size < sizeof(mkvc_frame_view) ||
        frame->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or frame view");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->write(*frame, false, error);
        if (result == MKVC_OK || result == MKVC_WOULD_BLOCK) {
            return result;
        }
        return fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown nonblocking frame write failure");
    }
}

mkvc_result mkvc_encoder_flush(mkvc_encoder* encoder) {
    last_error.clear();
    if (encoder == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "encoder is null");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->flush(error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown encoder flush failure");
    }
}

mkvc_result mkvc_encoder_cancel(mkvc_encoder* encoder) {
    last_error.clear();
    if (encoder == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "encoder is null");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->cancel(error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown encoder cancel failure");
    }
}

mkvc_result mkvc_encoder_close(mkvc_encoder* encoder) {
    last_error.clear();
    if (encoder == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "encoder is null");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->close(error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown encoder close failure");
    }
}

mkvc_result mkvc_encoder_get_metrics(const mkvc_encoder* encoder,
                                     mkvc_pipeline_metrics* out_metrics) {
    last_error.clear();
    if (encoder == nullptr || out_metrics == nullptr ||
        out_metrics->struct_size < sizeof(mkvc_pipeline_metrics) ||
        out_metrics->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder metrics output");
    }
    try {
        mkvc_pipeline_metrics metrics{};
        metrics.struct_size = sizeof(metrics);
        metrics.struct_version = 1;
        encoder->implementation->get_metrics(metrics);
        *out_metrics = metrics;
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown encoder metrics failure");
    }
}

void mkvc_encoder_destroy(mkvc_encoder* encoder) {
    try {
        delete encoder;
    } catch (...) {
    }
}

}  // extern "C"
