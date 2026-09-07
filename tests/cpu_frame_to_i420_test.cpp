#include "encoder/cpu_frame_to_i420.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "encoder/frame_timing.hpp"

int main() {
    constexpr uint32_t width = 4;
    constexpr uint32_t height = 2;
    std::vector<uint8_t> output(12);
    std::string error;

    const std::array<uint8_t, 12> i420_bytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 10, 12};
    mkvc_frame_view i420{};
    i420.pixel_format = MKVC_PIXEL_FORMAT_I420;
    i420.planes[0] = i420_bytes.data();
    i420.planes[1] = i420_bytes.data() + 8;
    i420.planes[2] = i420_bytes.data() + 10;
    i420.strides[0] = 4;
    i420.strides[1] = 2;
    i420.strides[2] = 2;
    if (mkvc::encoder::convert_cpu_frame_to_i420(i420, width, height, output, "test", error) !=
            MKVC_OK ||
        !std::equal(output.begin(), output.end(), i420_bytes.begin()))
        return 1;

    const std::array<uint8_t, 8> nv12_y{1, 2, 3, 4, 5, 6, 7, 8};
    const std::array<uint8_t, 4> nv12_uv{9, 10, 11, 12};
    mkvc_frame_view nv12{};
    nv12.pixel_format = MKVC_PIXEL_FORMAT_NV12;
    nv12.planes[0] = nv12_y.data();
    nv12.planes[1] = nv12_uv.data();
    nv12.strides[0] = 4;
    nv12.strides[1] = 4;
    if (mkvc::encoder::convert_cpu_frame_to_i420(nv12, width, height, output, "test", error) !=
            MKVC_OK ||
        !std::equal(output.begin(), output.end(), i420_bytes.begin()))
        return 2;

    std::array<uint8_t, 32> packed{};
    for (uint32_t format :
         {MKVC_PIXEL_FORMAT_BGR24, MKVC_PIXEL_FORMAT_RGB24, MKVC_PIXEL_FORMAT_BGRA32}) {
        mkvc_frame_view frame{};
        frame.pixel_format = format;
        frame.planes[0] = packed.data();
        frame.strides[0] =
            static_cast<int32_t>(width * (format == MKVC_PIXEL_FORMAT_BGRA32 ? 4u : 3u));
        if (mkvc::encoder::convert_cpu_frame_to_i420(frame, width, height, output, "test", error) !=
            MKVC_OK)
            return 3;
    }

    nv12.strides[1] = 3;
    if (mkvc::encoder::convert_cpu_frame_to_i420(nv12, width, height, output, "test", error) !=
        MKVC_ERROR_INVALID_ARGUMENT)
        return 4;
    std::vector<uint8_t> undersized(11);
    if (mkvc::encoder::convert_cpu_frame_to_i420(i420, width, height, undersized, "test", error) !=
        MKVC_ERROR_INTERNAL)
        return 5;

    mkvc::encoder::FrameTiming timing;
    timing.configure(30000, 1001);
    if (timing.select(-1) != 0 || timing.duration_nanoseconds() != 33366666ULL) return 6;
    timing.commit(0);
    if (timing.select(-1) != 1) return 7;
    timing.commit(10);
    timing.commit(5);
    if (timing.select(-1) != 11 || timing.to_nanoseconds(10) != 333666666ULL) return 8;
    return 0;
}
