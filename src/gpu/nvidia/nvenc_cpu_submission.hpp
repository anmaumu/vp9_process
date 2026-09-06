#pragma once

#include <cstdint>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu::nvidia {

class NvencApi;
struct NvencSession;

/**
 * @brief Upload and submit one contiguous CPU NV12 frame to NVENC.
 *
 * @param api Loaded NVENC function table.
 * @param session Initialized synchronous NVENC session.
 * @param nv12 Contiguous Y plane followed by interleaved UV rows.
 * @param width Visible even frame width and source row stride.
 * @param height Visible even frame height.
 * @param frame_index Monotonic picture index within the current session.
 * @param pts_ns Presentation timestamp in nanoseconds.
 * @param duration_ns Frame duration in nanoseconds.
 * @param force_keyframe Whether to request an IDR picture.
 * @param error Receives lock, layout or submission diagnostics.
 * @return MKVC_OK after NVENC accepts the picture, otherwise a public error.
 *
 * The function performs the explicit host-memory copy into NVENC's pitched input
 * buffer. A successfully locked buffer is always unlocked before return.
 */
mkvc_result submit_nvenc_cpu_frame(NvencApi& api, const NvencSession& session, const uint8_t* nv12,
                                   uint32_t width, uint32_t height, uint64_t frame_index,
                                   int64_t pts_ns, int64_t duration_ns, bool force_keyframe,
                                   std::string& error);

}  // namespace mkvc::gpu::nvidia
