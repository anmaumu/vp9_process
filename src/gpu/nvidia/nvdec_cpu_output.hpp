#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>

#include <cstdint>
#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc {
struct DecodedFrame;
}

namespace mkvc::gpu::nvidia {

class NvdecApi;

/**
 * @brief Consume one mapped NVDEC NV12 frame into an owned CPU I420 frame.
 *
 * @param api Loaded CUDA/NVCUVID function table.
 * @param decoder Decoder that owns the mapping.
 * @param device_pointer Mapped luma-plane device address.
 * @param pitch Device row pitch shared by luma and chroma planes.
 * @param width Visible frame width.
 * @param height Visible frame height.
 * @param pts_ns Presentation timestamp in nanoseconds.
 * @param frame Receives the owned I420 frame only on success.
 * @param error Receives a readback or unmap diagnostic.
 * @return MKVC_OK, or MKVC_ERROR_CODEC when CUDA readback/unmap fails.
 *
 * The function consumes the mapping and always attempts `cuvidUnmapVideoFrame`
 * before returning. It is intentionally the CPU copy boundary for NVDEC output.
 */
mkvc_result consume_nvdec_cpu_frame(NvdecApi& api, CUvideodecoder decoder,
                                    unsigned long long device_pointer, unsigned int pitch,
                                    uint32_t width, uint32_t height, int64_t pts_ns,
                                    std::unique_ptr<DecodedFrame>& frame, std::string& error);

}  // namespace mkvc::gpu::nvidia
