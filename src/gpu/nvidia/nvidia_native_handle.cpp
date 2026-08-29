#include "nvidia_native_handle.hpp"

namespace mkvc::gpu::nvidia {

mkvc_result make_cuda_handle(uint64_t device_id, uint64_t generation,
                             uint32_t type, uint64_t pointer_or_array,
                             uintptr_t context, uintptr_t stream,
                             uintptr_t event,
                             mkvc_gpu_native_handle_desc& output,
                             std::string& error) {
    if ((type != MKVC_GPU_NATIVE_CUDA_POINTER &&
         type != MKVC_GPU_NATIVE_CUDA_ARRAY) ||
        pointer_or_array == 0 || context == 0) {
        error = "invalid CUDA native handle";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    output = {};
    output.struct_size = sizeof(output);
    output.struct_version = 1;
    output.type = type;
    output.borrowed = 1;
    output.device_id = device_id;
    output.generation = generation;
    output.handles[0] = pointer_or_array;
    output.handles[1] = static_cast<uint64_t>(context);
    output.handles[2] = static_cast<uint64_t>(stream);
    output.handles[3] = static_cast<uint64_t>(event);
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
