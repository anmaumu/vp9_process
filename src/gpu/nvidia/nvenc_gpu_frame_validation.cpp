#include "nvenc_gpu_frame_validation.hpp"

#include <limits>

#include "gpu/gpu_frame.hpp"

namespace mkvc::gpu::nvidia {

mkvc_result prepare_nvenc_cuda_frame(const std::shared_ptr<GpuFrameCore>& frame,
                                     uint32_t required_width, uint32_t required_height,
                                     NvencCudaFrameView& output, std::string& error) {
    output = {};
    if (!frame) {
        error = "GPU frame is null";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const mkvc_gpu_frame_desc& desc = frame->desc();
    const bool cuda_pointer = desc.memory_type == MKVC_GPU_MEMORY_CUDA_POINTER;
    const bool cuda_array = desc.memory_type == MKVC_GPU_MEMORY_CUDA_ARRAY;
    if (desc.backend != MKVC_BACKEND_NVIDIA || (!cuda_pointer && !cuda_array) ||
        desc.pixel_format != MKVC_PIXEL_FORMAT_NV12 || desc.plane_count != 2) {
        error = "NVENC requires a NVIDIA CUDA pointer/array NV12 frame";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (desc.width != required_width || desc.height != required_height ||
        desc.pitches[0] < required_width || desc.pitches[0] != desc.pitches[1] ||
        desc.pitches[0] > std::numeric_limits<uint32_t>::max()) {
        error = "GPU frame dimensions or NV12 pitch do not match NVENC";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const auto producer = frame->producer_completion();
    if (!producer) {
        error = "GPU frame has no producer completion";
        return MKVC_ERROR_INVALID_STATE;
    }
    const mkvc_result wait_result = producer->wait(std::numeric_limits<uint32_t>::max(), error);
    if (wait_result != MKVC_OK) return wait_result;

    mkvc_gpu_native_handle_desc native{};
    const mkvc_result native_result = frame->get_native_handle(native, error);
    if (native_result != MKVC_OK) return native_result;
    const uint32_t expected_native_type =
        cuda_pointer ? MKVC_GPU_NATIVE_CUDA_POINTER : MKVC_GPU_NATIVE_CUDA_ARRAY;
    if (native.type != expected_native_type || native.handles[0] == 0 || native.handles[1] == 0 ||
        native.handles[0] !=
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(frame->backend_resource().object))) {
        error = "GPU frame has an inconsistent CUDA native handle";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }

    output.cuda_array = cuda_array;
    output.resource_handle = native.handles[0];
    output.context_handle = native.handles[1];
    output.pitch = static_cast<uint32_t>(desc.pitches[0]);
    output.pts_ns = desc.pts;
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
