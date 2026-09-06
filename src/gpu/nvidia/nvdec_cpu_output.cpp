#include "nvdec_cpu_output.hpp"

#include <cstddef>
#include <utility>
#include <vector>

#include "../../nvidia_webm_decoder.hpp"
#include "nvdec_api.hpp"

namespace mkvc::gpu::nvidia {

mkvc_result consume_nvdec_cpu_frame(NvdecApi& api, CUvideodecoder decoder,
                                    unsigned long long device_pointer, unsigned int pitch,
                                    uint32_t width, uint32_t height, int64_t pts_ns,
                                    std::unique_ptr<DecodedFrame>& frame, std::string& error) {
    frame.reset();
    auto output = std::make_unique<DecodedFrame>();
    output->width = width;
    output->height = height;
    output->pts_ns = pts_ns;
    const size_t y_size = static_cast<size_t>(width) * height;
    const size_t chroma_size = y_size / 4;
    output->pixels.resize(y_size + chroma_size * 2);
    output->offsets = {0, y_size, y_size + chroma_size};
    output->strides = {static_cast<int32_t>(width), static_cast<int32_t>(width / 2),
                       static_cast<int32_t>(width / 2)};
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.srcDevice = static_cast<CUdeviceptr>(device_pointer);
    copy.srcPitch = pitch;
    copy.dstMemoryType = CU_MEMORYTYPE_HOST;
    copy.dstHost = output->pixels.data();
    copy.dstPitch = width;
    copy.WidthInBytes = width;
    copy.Height = height;
    bool copied = api.memcpy_2d(&copy) == CUDA_SUCCESS;
    std::vector<uint8_t> uv(y_size / 2);
    if (copied) {
        copy.srcDevice =
            static_cast<CUdeviceptr>(device_pointer) + static_cast<CUdeviceptr>(pitch) * height;
        copy.dstHost = uv.data();
        copy.dstPitch = width;
        copy.Height = height / 2;
        copied = api.memcpy_2d(&copy) == CUDA_SUCCESS;
    }
    const CUresult unmapped = api.unmap_frame(decoder, device_pointer);
    if (!copied || unmapped != CUDA_SUCCESS) {
        error = "NVDEC frame readback failed";
        return MKVC_ERROR_CODEC;
    }
    uint8_t* u = output->pixels.data() + output->offsets[1];
    uint8_t* v = output->pixels.data() + output->offsets[2];
    for (size_t index = 0; index < chroma_size; ++index) {
        u[index] = uv[index * 2];
        v[index] = uv[index * 2 + 1];
    }
    frame = std::move(output);
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
