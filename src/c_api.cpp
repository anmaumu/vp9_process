#include "mkvcodec/mkvc.h"

#include "backend_registry.hpp"
#include "cpu_vp9_encoder.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>

struct mkvc_encoder {
    std::unique_ptr<mkvc::CpuVp9Encoder> implementation;
};

namespace {
thread_local std::string last_error;

mkvc_result fail(mkvc_result result, std::string message) {
    last_error = std::move(message);
    return result;
}
}  // namespace

extern "C" {

mkvc_result mkvc_get_version(mkvc_version* out_version) {
    if (out_version == nullptr || out_version->struct_size < sizeof(mkvc_version)) {
        return MKVC_ERROR_INVALID_ARGUMENT;
    }

    out_version->abi_version = MKVC_ABI_VERSION;
    out_version->major = 0;
    out_version->minor = 1;
    out_version->patch = 0;
    return MKVC_OK;
}

mkvc_result mkvc_get_backend_capabilities(
    mkvc_backend_capability* capabilities,
    size_t* inout_count) {
    if (inout_count == nullptr) {
        return MKVC_ERROR_INVALID_ARGUMENT;
    }

    const auto& available = mkvc::backend_capabilities();
    const size_t required = available.size();
    if (capabilities == nullptr) {
        *inout_count = required;
        return MKVC_OK;
    }
    if (*inout_count < required) {
        *inout_count = required;
        return MKVC_ERROR_BUFFER_TOO_SMALL;
    }

    std::copy(available.begin(), available.end(), capabilities);
    *inout_count = required;
    return MKVC_OK;
}

const char* mkvc_result_string(mkvc_result result) {
    switch (result) {
        case MKVC_OK: return "ok";
        case MKVC_ERROR_INVALID_ARGUMENT: return "invalid argument";
        case MKVC_ERROR_BUFFER_TOO_SMALL: return "buffer too small";
        case MKVC_ERROR_NOT_SUPPORTED: return "not supported";
        case MKVC_ERROR_INTERNAL: return "internal error";
        case MKVC_ERROR_INVALID_STATE: return "invalid state";
        case MKVC_ERROR_IO: return "I/O error";
        case MKVC_ERROR_CODEC: return "codec error";
        default: return "unknown result";
    }
}

mkvc_result mkvc_encoder_create(const mkvc_encoder_config* config,
                                mkvc_encoder** out_encoder) {
    last_error.clear();
    if (out_encoder != nullptr) {
        *out_encoder = nullptr;
    }
    if (config == nullptr || out_encoder == nullptr ||
        config->struct_size < sizeof(mkvc_encoder_config) ||
        config->struct_version != 1 || config->output_path_utf8 == nullptr ||
        config->output_path_utf8[0] == '\0' || config->codec != MKVC_CODEC_VP9 ||
        config->backend != MKVC_BACKEND_CPU || config->width == 0 ||
        config->height == 0 || (config->width & 1u) != 0 ||
        (config->height & 1u) != 0 || config->fps_num == 0 ||
        config->fps_den == 0 || config->quality > 63) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid CPU VP9 encoder config");
    }
    try {
        std::string error;
        auto implementation = mkvc::CpuVp9Encoder::create(*config, error);
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

mkvc_result mkvc_encoder_write_frame(mkvc_encoder* encoder,
                                     const mkvc_frame_view* frame) {
    last_error.clear();
    if (encoder == nullptr || frame == nullptr ||
        frame->struct_size < sizeof(mkvc_frame_view) || frame->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or frame view");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->write(*frame, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown frame write failure");
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

void mkvc_encoder_destroy(mkvc_encoder* encoder) {
    try {
        delete encoder;
    } catch (...) {
    }
}

const char* mkvc_get_last_error(void) {
    return last_error.c_str();
}

}  // extern "C"
