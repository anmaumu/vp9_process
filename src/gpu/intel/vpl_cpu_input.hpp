/**
 * @file vpl_cpu_input.hpp
 * @brief CPU NV12 upload and submission for oneVPL encoders.
 */
#pragma once

#include <vpl/mfxvideo.h>

#include <cstdint>
#include <string>
#include <vector>

#include "intel_vpl_encoder.hpp"

namespace mkvc::gpu::intel {

class VplEncoderQueue;

/**
 * @brief Copy and submit one CPU-resident NV12 frame.
 *
 * This is the explicit CPU-copy path. It acquires an internally managed encode
 * surface, maps it for writing, copies visible Y/UV rows, assigns a 90 kHz
 * timestamp, unmaps, submits, and releases the caller-side surface reference.
 */
mkvc_result submit_cpu_nv12(mfxSession session, VplEncoderQueue& queue, uint32_t width,
                            uint32_t height, uint32_t fps_num, uint32_t fps_den, int64_t& next_pts,
                            const uint8_t* y, int32_t y_stride, const uint8_t* uv,
                            int32_t uv_stride, int64_t pts,
                            std::vector<IntelEncodedPacket>& packets, std::string& error);

}  // namespace mkvc::gpu::intel
