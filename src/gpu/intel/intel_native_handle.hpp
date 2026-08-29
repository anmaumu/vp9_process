#pragma once

#include "mkvcodec/mkvc.h"

#include <cstdint>
#include <string>

namespace mkvc::gpu::intel {

mkvc_result make_d3d11_handle(uint64_t device_id, uint64_t generation,
                              uintptr_t texture, uint32_t subresource,
                              mkvc_gpu_native_handle_desc& output,
                              std::string& error);
mkvc_result make_va_surface_handle(uint64_t device_id, uint64_t generation,
                                  uintptr_t display, uint32_t surface,
                                  mkvc_gpu_native_handle_desc& output,
                                  std::string& error);

}  // namespace mkvc::gpu::intel
