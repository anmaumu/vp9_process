/**
 * @file gpu_frame_c_api.cpp
 * @brief Stable C ABI adapter for GPU-frame leases and completion state.
 */
#include "gpu_frame_handle.hpp"

#include <string>
#include <utility>

extern thread_local std::string mkvc_last_error;

namespace {

mkvc_result gpu_fail(const mkvc_result result, std::string message) {
    mkvc_last_error = std::move(message);
    return result;
}

bool valid(const mkvc_gpu_frame* frame) {
    return frame != nullptr && frame->core != nullptr &&
           frame->generation == frame->core->desc().generation && !frame->core->recycled();
}

}  // namespace

extern "C" {

mkvc_result mkvc_gpu_frame_retain(mkvc_gpu_frame* frame) {
    if (!valid(frame)) return gpu_fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
    frame->references.fetch_add(1, std::memory_order_relaxed);
    return MKVC_OK;
}

void mkvc_gpu_frame_release(mkvc_gpu_frame* frame) {
    if (frame != nullptr && frame->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        frame->core->release_external();
        delete frame;
    }
}

mkvc_result mkvc_gpu_frame_get_desc(const mkvc_gpu_frame* frame, mkvc_gpu_frame_desc* out_desc) {
    if (!valid(frame)) return gpu_fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
    if (out_desc == nullptr || out_desc->struct_size < sizeof(*out_desc) ||
        out_desc->struct_version != 1) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid GPU frame descriptor");
    }
    *out_desc = frame->core->desc();
    return MKVC_OK;
}

mkvc_result mkvc_gpu_frame_query_completion(const mkvc_gpu_frame* frame, uint32_t* out_status) {
    if (!valid(frame)) return gpu_fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
    if (out_status == nullptr)
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, "completion output is null");
    std::string error;
    *out_status = frame->core->producer_completion()->query(error);
    frame->core->poll_recycle();
    return *out_status == MKVC_GPU_COMPLETION_FAILED ? gpu_fail(MKVC_ERROR_CODEC, std::move(error))
                                                     : MKVC_OK;
}

mkvc_result mkvc_gpu_frame_wait(const mkvc_gpu_frame* frame, const uint32_t timeout_ms) {
    if (!valid(frame)) return gpu_fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
    std::string error;
    const mkvc_result result = frame->core->producer_completion()->wait(timeout_ms, error);
    frame->core->poll_recycle();
    return result == MKVC_OK ? result : gpu_fail(result, std::move(error));
}

mkvc_result mkvc_gpu_frame_get_native_handle(const mkvc_gpu_frame* frame,
                                             mkvc_gpu_native_handle_desc* out_handle) {
    if (!valid(frame)) return gpu_fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
    if (out_handle == nullptr || out_handle->struct_size < sizeof(*out_handle) ||
        out_handle->struct_version != 1) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid native handle output");
    }
    std::string error;
    mkvc_gpu_native_handle_desc value{};
    const mkvc_result result = frame->core->get_native_handle(value, error);
    if (result != MKVC_OK) return gpu_fail(result, std::move(error));
    *out_handle = value;
    return MKVC_OK;
}

}  // extern "C"
