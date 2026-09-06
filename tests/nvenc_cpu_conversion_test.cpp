#include "gpu/nvidia/nvenc_cpu_conversion.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

int main() {
    constexpr uint32_t width = 4;
    constexpr uint32_t height = 2;
    std::vector<uint8_t> scratch(12);
    std::vector<uint8_t> output(12);
    std::string error;

    const std::array<uint8_t, 12> padded_y{1, 2, 3, 4, 90, 90, 5, 6, 7, 8, 91, 91};
    const std::array<uint8_t, 6> padded_uv{9, 10, 11, 12, 92, 92};
    mkvc_frame_view nv12{};
    nv12.pixel_format = MKVC_PIXEL_FORMAT_NV12;
    nv12.planes[0] = padded_y.data();
    nv12.planes[1] = padded_uv.data();
    nv12.strides[0] = 6;
    nv12.strides[1] = 6;
    const std::array<uint8_t, 12> expected{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    if (mkvc::gpu::nvidia::convert_nvenc_input_to_nv12(nv12, width, height, scratch, output,
                                                       error) != MKVC_OK ||
        !std::equal(output.begin(), output.end(), expected.begin()))
        return 1;

    const std::array<uint8_t, 8> y{1, 2, 3, 4, 5, 6, 7, 8};
    const std::array<uint8_t, 2> u{9, 11};
    const std::array<uint8_t, 2> v{10, 12};
    mkvc_frame_view i420{};
    i420.pixel_format = MKVC_PIXEL_FORMAT_I420;
    i420.planes[0] = y.data();
    i420.planes[1] = u.data();
    i420.planes[2] = v.data();
    i420.strides[0] = 4;
    i420.strides[1] = 2;
    i420.strides[2] = 2;
    if (mkvc::gpu::nvidia::convert_nvenc_input_to_nv12(i420, width, height, scratch, output,
                                                       error) != MKVC_OK ||
        !std::equal(output.begin(), output.end(), expected.begin()))
        return 2;

    nv12.strides[1] = 3;
    if (mkvc::gpu::nvidia::convert_nvenc_input_to_nv12(nv12, width, height, scratch, output,
                                                       error) != MKVC_ERROR_INVALID_ARGUMENT)
        return 3;
    return 0;
}
