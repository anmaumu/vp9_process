/**
 * @file c_api.cpp
 * @brief C ABI version, capability discovery, and diagnostics entry points.
 */
#include <algorithm>
#include <string>

#include "backend_registry.hpp"
#include "c_api_internal.hpp"
#include "mkvcodec/mkvc.h"

thread_local std::string mkvc_last_error;

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

mkvc_result mkvc_get_backend_capabilities(mkvc_backend_capability* capabilities,
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
        case MKVC_OK:
            return "ok";
        case MKVC_ERROR_INVALID_ARGUMENT:
            return "invalid argument";
        case MKVC_ERROR_BUFFER_TOO_SMALL:
            return "buffer too small";
        case MKVC_ERROR_NOT_SUPPORTED:
            return "not supported";
        case MKVC_ERROR_INTERNAL:
            return "internal error";
        case MKVC_ERROR_INVALID_STATE:
            return "invalid state";
        case MKVC_ERROR_IO:
            return "I/O error";
        case MKVC_ERROR_CODEC:
            return "codec error";
        case MKVC_END_OF_STREAM:
            return "end of stream";
        case MKVC_WOULD_BLOCK:
            return "would block";
        case MKVC_ERROR_TIMEOUT:
            return "timeout";
        case MKVC_ERROR_CANCELLED:
            return "cancelled";
        default:
            return "unknown result";
    }
}

const char* mkvc_get_last_error(void) { return mkvc_last_error.c_str(); }

}  // extern "C"
