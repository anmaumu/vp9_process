/**
 * @file cpu_frame_to_i420.hpp
 * @brief Backend-neutral CPU frame validation and I420 staging conversion.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc::encoder {

/**
 * @brief Copy or convert one supported CPU frame into contiguous I420 memory.
 * @param frame Source BGR24, RGB24, BGRA32, I420, or NV12 frame view.
 * @param width Required even visible width.
 * @param height Required even visible height.
 * @param i420 Destination storage of at least width*height*3/2 bytes.
 * @param codec_name Diagnostic label such as VP9 or AV1.
 * @param error Receives buffer, plane, stride, format, or conversion diagnostics.
 * @return MKVC_OK, a public input error, or MKVC_ERROR_INTERNAL.
 */
mkvc_result convert_cpu_frame_to_i420(const mkvc_frame_view& frame, uint32_t width, uint32_t height,
                                      std::vector<uint8_t>& i420, const char* codec_name,
                                      std::string& error);

}  // namespace mkvc::encoder
