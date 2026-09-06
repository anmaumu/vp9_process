/**
 * @file c_api_frame.cpp
 * @brief C ABI adapters for decoded CPU-frame ownership, access, and processing.
 */
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "c_api_internal.hpp"
#include "frame_conversion.hpp"
#include "frame_processor.hpp"

namespace {
#define last_error mkvc_last_error
using mkvc::capi::fail;
}  // namespace

extern "C" {

void mkvc_frame_retain(mkvc_frame* frame) {
    if (frame != nullptr) frame->references.fetch_add(1, std::memory_order_relaxed);
}

void mkvc_frame_release(mkvc_frame* frame) {
    if (frame != nullptr && frame->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete frame;
    }
}

mkvc_result mkvc_frame_get_view(const mkvc_frame* frame, mkvc_frame_view* out_view) {
    last_error.clear();
    if (frame == nullptr || out_view == nullptr ||
        out_view->struct_size < sizeof(mkvc_frame_view) || out_view->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid frame or output view");
    }
    const auto& source = *frame->implementation;
    out_view->pixel_format = MKVC_PIXEL_FORMAT_I420;
    out_view->width = source.width;
    out_view->height = source.height;
    for (size_t plane = 0; plane < 3; ++plane) {
        out_view->planes[plane] = source.pixels.data() + source.offsets[plane];
        out_view->strides[plane] = source.strides[plane];
    }
    out_view->planes[3] = nullptr;
    out_view->strides[3] = 0;
    out_view->pts = source.pts_ns;
    return MKVC_OK;
}

mkvc_result mkvc_frame_copy_to(const mkvc_frame* frame, mkvc_mutable_frame_view* destination) {
    last_error.clear();
    if (frame == nullptr || destination == nullptr ||
        destination->struct_size < sizeof(mkvc_mutable_frame_view) ||
        destination->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid frame or mutable destination view");
    }
    try {
        std::string error;
        const mkvc_result result = mkvc::copy_frame_to(*frame->implementation, *destination, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown frame conversion failure");
    }
}

mkvc_result mkvc_frame_process(const mkvc_frame* frame, const mkvc_frame_process_config* config,
                               mkvc_frame** out_frame) {
    last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (frame == nullptr || frame->implementation == nullptr || config == nullptr ||
        out_frame == nullptr || config->struct_size < sizeof(mkvc_frame_process_config) ||
        config->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid frame process arguments");
    }
    try {
        std::unique_ptr<mkvc::DecodedFrame> processed;
        std::string error;
        const mkvc_result result =
            mkvc::process_frame_cpu(*frame->implementation, *config, processed, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        auto handle = std::make_unique<mkvc_frame>();
        handle->implementation = std::move(processed);
        *out_frame = handle.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown frame processing failure");
    }
}

}  // extern "C"
