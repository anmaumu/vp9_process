#include "gpu_frame.hpp"

#if defined(MKVC_HAS_NVIDIA)
#include "nvidia/cuda_completion.hpp"
#endif

#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string>

extern thread_local std::string mkvc_last_error;

namespace {

enum DLDeviceType : int32_t { kDLCUDA = 2, kDLOneAPI = 14 };
struct DLDevice { DLDeviceType device_type; int32_t device_id; };
struct DLDataType { uint8_t code; uint8_t bits; uint16_t lanes; };
struct DLTensor {
    void* data;
    DLDevice device;
    int32_t ndim;
    DLDataType dtype;
    int64_t* shape;
    int64_t* strides;
    uint64_t byte_offset;
};
struct DLManagedTensor {
    DLTensor dl_tensor;
    void* manager_ctx;
    void (*deleter)(DLManagedTensor* self);
};
struct ManagedPlane {
    DLManagedTensor managed{};
    mkvc_gpu_frame* frame = nullptr;
    int64_t shape[2]{};
    int64_t strides[2]{};
};

void delete_managed(DLManagedTensor* tensor) {
    if (tensor == nullptr) return;
    auto* state = static_cast<ManagedPlane*>(tensor->manager_ctx);
    if (state == nullptr) return;
    mkvc_gpu_frame_release(state->frame);
    delete state;
}

mkvc_result fail(mkvc_result result, const char* message) {
    mkvc_last_error = message;
    return result;
}

}  // namespace

extern "C" mkvc_result mkvc_gpu_frame_export_dlpack(
    mkvc_gpu_frame* frame, uint32_t plane_index,
    uint64_t consumer_stream, void** out_managed_tensor) {
    if (out_managed_tensor == nullptr) return fail(MKVC_ERROR_INVALID_ARGUMENT, "null DLPack output");
    *out_managed_tensor = nullptr;
    mkvc_gpu_frame_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.struct_version = 1;
    mkvc_result result = mkvc_gpu_frame_get_desc(frame, &desc);
    if (result != MKVC_OK) return result;
    if (plane_index >= desc.plane_count || desc.pixel_format != MKVC_PIXEL_FORMAT_NV12)
        return fail(MKVC_ERROR_NOT_SUPPORTED, "DLPack export supports NV12 planes only");
    mkvc_gpu_native_handle_desc native{};
    native.struct_size = sizeof(native);
    native.struct_version = 1;
    result = mkvc_gpu_frame_get_native_handle(frame, &native);
    if (result != MKVC_OK) return result;
    const bool cuda = desc.backend == MKVC_BACKEND_NVIDIA &&
        desc.memory_type == MKVC_GPU_MEMORY_CUDA_POINTER &&
        native.type == MKVC_GPU_NATIVE_CUDA_POINTER;
    const bool usm = desc.backend == MKVC_BACKEND_INTEL &&
        desc.memory_type == MKVC_GPU_MEMORY_USM &&
        native.type == MKVC_GPU_NATIVE_USM_POINTER;
    if ((!cuda && !usm) || native.handles[0] == 0)
        return fail(MKVC_ERROR_NOT_SUPPORTED,
                    "GPU memory is not a linear CUDA or Intel device-USM pointer");
    if (desc.device_id > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) ||
        desc.pitches[plane_index] >
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        desc.plane_offsets[plane_index] >
            std::numeric_limits<uint64_t>::max() - native.handles[0])
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "DLPack metadata exceeds its ABI range");
    if (cuda && consumer_stream != 0 && native.handles[3] != 0) {
#if defined(MKVC_HAS_NVIDIA)
        std::string error;
        result = mkvc::gpu::nvidia::cuda_stream_wait_event(
            native.handles[1], native.handles[3], consumer_stream, error);
        if (result != MKVC_OK) {
            mkvc_last_error = error;
            return result;
        }
#else
        return fail(MKVC_ERROR_NOT_SUPPORTED,
                    "CUDA stream dependencies are disabled in this build");
#endif
    } else {
        // Public USM import is synchronized-only. Waiting here proves the
        // producer is complete before any oneAPI consumer queue sees it.
        result = mkvc_gpu_frame_wait(frame, std::numeric_limits<uint32_t>::max());
        if (result != MKVC_OK) return result;
    }
    result = mkvc_gpu_frame_retain(frame);
    if (result != MKVC_OK) return result;
    std::unique_ptr<ManagedPlane> state(new (std::nothrow) ManagedPlane());
    if (!state) {
        mkvc_gpu_frame_release(frame);
        return fail(MKVC_ERROR_INTERNAL, "DLPack allocation failed");
    }
    state->frame = frame;
    state->shape[0] = plane_index == 0 ? desc.height : desc.height / 2;
    state->shape[1] = desc.width;
    state->strides[0] = static_cast<int64_t>(desc.pitches[plane_index]);
    state->strides[1] = 1;
    state->managed.dl_tensor.data = reinterpret_cast<void*>(
        static_cast<uintptr_t>(native.handles[0] + desc.plane_offsets[plane_index]));
    state->managed.dl_tensor.device = {
        cuda ? kDLCUDA : kDLOneAPI, static_cast<int32_t>(desc.device_id)};
    state->managed.dl_tensor.ndim = 2;
    state->managed.dl_tensor.dtype = {1, 8, 1};  // kDLUInt, uint8, one lane.
    state->managed.dl_tensor.shape = state->shape;
    state->managed.dl_tensor.strides = state->strides;
    state->managed.dl_tensor.byte_offset = 0;
    state->managed.manager_ctx = state.get();
    state->managed.deleter = delete_managed;
    *out_managed_tensor = &state.release()->managed;
    return MKVC_OK;
}

extern "C" void mkvc_dlpack_managed_tensor_release(void* managed_tensor) {
    auto* tensor = static_cast<DLManagedTensor*>(managed_tensor);
    if (tensor != nullptr && tensor->deleter != nullptr) tensor->deleter(tensor);
}
