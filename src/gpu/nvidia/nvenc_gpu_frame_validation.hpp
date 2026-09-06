#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu {

class GpuFrameCore;

namespace nvidia {

/** Validated CUDA resource information required by synchronous NVENC submission. */
struct NvencCudaFrameView {
    bool cuda_array = false;
    uint64_t resource_handle = 0;
    uint64_t context_handle = 0;
    uint32_t pitch = 0;
    int64_t pts_ns = -1;
};

/**
 * @brief Validate and synchronize one common GPU frame for NVENC.
 *
 * @param frame Retained common GPU frame core.
 * @param required_width Configured encoder width.
 * @param required_height Configured encoder height.
 * @param output Receives normalized CUDA pointer/array information.
 * @param error Receives format, layout, handle or completion diagnostics.
 * @return MKVC_OK after producer completion, otherwise a public error.
 *
 * The function accepts only NVIDIA NV12 CUDA pointer/array resources with two
 * equally pitched planes. It waits for producer completion before borrowing the
 * native resource, but does not retain a new lease or change CUDA context state.
 */
mkvc_result prepare_nvenc_cuda_frame(const std::shared_ptr<GpuFrameCore>& frame,
                                     uint32_t required_width, uint32_t required_height,
                                     NvencCudaFrameView& output, std::string& error);

}  // namespace nvidia
}  // namespace mkvc::gpu
