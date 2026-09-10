#pragma once

#include <cstdint>
#include <memory>

#include "cpu_vp9_decoder.hpp"

namespace mkvc::frame_processor_detail {

/** Return whether a dimension is nonzero and compatible with I420. */
bool even_nonzero(uint32_t value);

/** Allocate one tightly packed I420 frame, or return null for invalid sizes. */
std::unique_ptr<DecodedFrame> allocate_i420(uint32_t width, uint32_t height, int64_t pts_ns);

/** Copy the overlapping I420 rectangle between two frames. */
int copy_i420(const DecodedFrame& source, DecodedFrame& destination, uint32_t src_x = 0,
              uint32_t src_y = 0, uint32_t dst_x = 0, uint32_t dst_y = 0);

/** Crop an even I420 rectangle into a new owned frame. */
std::unique_ptr<DecodedFrame> crop_frame(const DecodedFrame& source, uint32_t x, uint32_t y,
                                         uint32_t width, uint32_t height);

/** Rotate an I420 frame clockwise by a supported right angle. */
std::unique_ptr<DecodedFrame> rotate_frame(const DecodedFrame& source, uint32_t rotation);

/** Mirror an I420 frame horizontally. */
std::unique_ptr<DecodedFrame> mirror_frame(const DecodedFrame& source);

/** Bilinearly scale an I420 frame to an even target size. */
std::unique_ptr<DecodedFrame> scale_frame(const DecodedFrame& source, uint32_t width,
                                          uint32_t height);

/** Fill an I420 frame from an RGBA background color. */
void fill_background(DecodedFrame& frame, uint32_t rgba);

/** Round an unsigned coordinate or size down to the nearest even value. */
uint32_t even_floor(uint64_t value);

}  // namespace mkvc::frame_processor_detail
