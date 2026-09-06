#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu::nvidia {

/**
 * @brief Convert one supported CPU frame into contiguous NV12 staging memory.
 *
 * @param frame Source BGR24, RGB24, BGRA32, I420 or NV12 frame view.
 * @param width Required even frame width.
 * @param height Required even frame height.
 * @param i420 Reusable I420 scratch buffer of at least width*height*3/2 bytes.
 * @param nv12 Destination buffer of at least width*height*3/2 bytes.
 * @param error Receives format, plane, stride or conversion diagnostics.
 * @return MKVC_OK, a public input error, or MKVC_ERROR_INTERNAL.
 */
mkvc_result convert_nvenc_input_to_nv12(const mkvc_frame_view& frame, uint32_t width,
                                        uint32_t height, std::vector<uint8_t>& i420,
                                        std::vector<uint8_t>& nv12, std::string& error);

}  // namespace mkvc::gpu::nvidia
