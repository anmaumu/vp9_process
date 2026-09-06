#include "nvenc_cpu_conversion.hpp"

#include "encoder/cpu_frame_to_nv12.hpp"

namespace mkvc::gpu::nvidia {

mkvc_result convert_nvenc_input_to_nv12(const mkvc_frame_view& frame, uint32_t width,
                                        uint32_t height, std::vector<uint8_t>& i420,
                                        std::vector<uint8_t>& nv12, std::string& error) {
    return encoder::convert_cpu_frame_to_nv12(frame, width, height, i420, nv12, "NVIDIA", error);
}

}  // namespace mkvc::gpu::nvidia
