#include "intel_native_handle.hpp"

namespace mkvc::gpu::intel {
namespace {
void initialize(mkvc_gpu_native_handle_desc& output, uint32_t type,
                uint64_t device_id, uint64_t generation) {
    output = {};
    output.struct_size = sizeof(output);
    output.struct_version = 1;
    output.type = type;
    output.borrowed = 1;
    output.device_id = device_id;
    output.generation = generation;
}
}

mkvc_result make_d3d11_handle(uint64_t device_id, uint64_t generation,
                              uintptr_t texture, uint32_t subresource,
                              mkvc_gpu_native_handle_desc& output,
                              std::string& error) {
    if (texture == 0) { error = "D3D11 texture is null"; return MKVC_ERROR_INVALID_ARGUMENT; }
    initialize(output, MKVC_GPU_NATIVE_D3D11_TEXTURE, device_id, generation);
    output.handles[0] = static_cast<uint64_t>(texture);
    output.handles[1] = subresource;
    return MKVC_OK;
}

mkvc_result make_va_surface_handle(uint64_t device_id, uint64_t generation,
                                  uintptr_t display, uint32_t surface,
                                  mkvc_gpu_native_handle_desc& output,
                                  std::string& error) {
    if (display == 0) { error = "VA display is null"; return MKVC_ERROR_INVALID_ARGUMENT; }
    initialize(output, MKVC_GPU_NATIVE_VA_SURFACE, device_id, generation);
    output.handles[0] = static_cast<uint64_t>(display);
    output.handles[1] = surface;
    return MKVC_OK;
}

}  // namespace mkvc::gpu::intel
