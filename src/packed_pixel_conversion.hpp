/**
 * @file packed_pixel_conversion.hpp
 * @brief Internal SIMD-dispatched packed-pixel helpers.
 */
#pragma once

#include <cstdint>

namespace mkvc {

/**
 * Remove the fourth byte from every four-byte pixel.
 *
 * The first three bytes retain their input order. Source and destination must
 * not overlap. Row padding is supported through the independent strides.
 *
 * @param source Four-byte packed source pixels.
 * @param source_stride Source bytes between rows.
 * @param destination Three-byte packed destination pixels.
 * @param destination_stride Destination bytes between rows.
 * @param width Number of pixels per row.
 * @param height Number of rows.
 */
void pack_four_to_three(const uint8_t* source, int source_stride, uint8_t* destination,
                        int destination_stride, int width, int height);

}  // namespace mkvc
