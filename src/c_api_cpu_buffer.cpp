/**
 * @file c_api_cpu_buffer.cpp
 * @brief C ABI descriptor, writable view, and release operations for CPU buffers.
 */
#include <exception>
#include <string>
#include <utility>

#include "c_api_internal.hpp"

namespace {
#define last_error mkvc_last_error
using mkvc::capi::fail;
}  // namespace

extern "C" {

mkvc_result mkvc_cpu_buffer_get_desc(const mkvc_cpu_buffer* buffer,
                                     mkvc_cpu_buffer_desc* out_desc) {
    last_error.clear();
    if (buffer == nullptr || !buffer->implementation || out_desc == nullptr ||
        out_desc->struct_size < sizeof(*out_desc) || out_desc->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid native CPU buffer descriptor output");
    }
    mkvc_cpu_buffer_desc value{};
    value.struct_size = sizeof(value);
    value.struct_version = 1;
    value.pixel_format = buffer->implementation->pixel_format();
    value.width = buffer->implementation->width();
    value.height = buffer->implementation->height();
    value.plane_count = buffer->implementation->plane_count();
    value.generation = buffer->implementation->generation();
    *out_desc = value;
    return MKVC_OK;
}

mkvc_result mkvc_cpu_buffer_get_view(const mkvc_cpu_buffer* buffer,
                                     mkvc_mutable_frame_view* out_view) {
    last_error.clear();
    if (buffer == nullptr || !buffer->implementation || out_view == nullptr ||
        out_view->struct_size < sizeof(*out_view) || out_view->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid native CPU buffer view output");
    }
    try {
        mkvc_mutable_frame_view value{};
        std::string error;
        const mkvc_result result = buffer->implementation->get_view(value, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        *out_view = value;
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown CPU buffer view failure");
    }
}

void mkvc_cpu_buffer_release(mkvc_cpu_buffer* buffer) {
    try {
        delete buffer;
    } catch (...) {
    }
}

}  // extern "C"
