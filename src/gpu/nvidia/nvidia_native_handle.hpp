#pragma once

#include "mkvcodec/mkvc.h"

#include <cstdint>
#include <string>

namespace mkvc::gpu::nvidia {

mkvc_result make_cuda_handle(uint64_t device_id, uint64_t generation,
                             uint32_t type, uint64_t pointer_or_array,
                             uintptr_t context, uintptr_t stream,
                             uintptr_t event,
                             mkvc_gpu_native_handle_desc& output,
                             std::string& error);

}  // namespace mkvc::gpu::nvidia
