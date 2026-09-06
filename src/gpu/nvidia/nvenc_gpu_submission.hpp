#pragma once

#include <cstdint>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu::nvidia {

class NvencApi;
struct NvencSession;

/**
 * @brief Register, map and submit one CUDA NV12 resource to NVENC.
 *
 * @param api Loaded CUDA/NVENC function table.
 * @param session Initialized NVENC session using the resource's CUDA context.
 * @param cuda_array True for CUarray, false for a CUDA device pointer.
 * @param resource_handle CUDA resource handle encoded as an integer.
 * @param width Visible frame width.
 * @param height Visible frame height.
 * @param pitch NV12 row pitch.
 * @param frame_index Monotonic picture index within the current session.
 * @param pts_ns Presentation timestamp in nanoseconds.
 * @param duration_ns Frame duration in nanoseconds.
 * @param force_keyframe Whether to request an IDR picture.
 * @param error Receives registration, mapping or encoding diagnostics.
 * @return MKVC_OK after NVENC accepts the picture, otherwise MKVC_ERROR_CODEC.
 *
 * A successfully registered resource is always unregistered before return, and
 * a successfully mapped resource is always unmapped. Pixel data is not copied.
 */
mkvc_result submit_nvenc_cuda_frame(NvencApi& api, const NvencSession& session, bool cuda_array,
                                    uint64_t resource_handle, uint32_t width, uint32_t height,
                                    uint32_t pitch, uint64_t frame_index, int64_t pts_ns,
                                    int64_t duration_ns, bool force_keyframe, std::string& error);

}  // namespace mkvc::gpu::nvidia
