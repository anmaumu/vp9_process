/**
 * @file packed_pixel_conversion.cpp
 * @brief Highway implementation of four-to-three-byte pixel packing.
 */
#include "packed_pixel_conversion.hpp"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "packed_pixel_conversion.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace mkvc {
namespace HWY_NAMESPACE {
namespace {

namespace hn = hwy::HWY_NAMESPACE;

/** Pack complete vectors and use a scalar tail without touching row padding. */
void PackFourToThree(const uint8_t* HWY_RESTRICT source, const int source_stride,
                     uint8_t* HWY_RESTRICT destination, const int destination_stride,
                     const int width, const int height) {
    const hn::ScalableTag<uint8_t> bytes;
    const size_t lanes = hn::Lanes(bytes);
    for (int row = 0; row < height; ++row) {
        const uint8_t* input = source + static_cast<size_t>(row) * source_stride;
        uint8_t* output = destination + static_cast<size_t>(row) * destination_stride;
        size_t column = 0;
        for (; column + lanes <= static_cast<size_t>(width); column += lanes) {
            hn::Vec<decltype(bytes)> byte0;
            hn::Vec<decltype(bytes)> byte1;
            hn::Vec<decltype(bytes)> byte2;
            hn::Vec<decltype(bytes)> ignored;
            hn::LoadInterleaved4(bytes, input + column * 4, byte0, byte1, byte2, ignored);
            hn::StoreInterleaved3(byte0, byte1, byte2, bytes, output + column * 3);
        }
        for (; column < static_cast<size_t>(width); ++column) {
            output[column * 3] = input[column * 4];
            output[column * 3 + 1] = input[column * 4 + 1];
            output[column * 3 + 2] = input[column * 4 + 2];
        }
    }
}

}  // namespace
}  // namespace HWY_NAMESPACE
}  // namespace mkvc
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace mkvc {

HWY_EXPORT(PackFourToThree);

void pack_four_to_three(const uint8_t* source, const int source_stride, uint8_t* destination,
                        const int destination_stride, const int width, const int height) {
    HWY_DYNAMIC_DISPATCH(PackFourToThree)(source, source_stride, destination, destination_stride,
                                          width, height);
}

}  // namespace mkvc
#endif
