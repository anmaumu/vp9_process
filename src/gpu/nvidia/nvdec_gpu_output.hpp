#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu {
class GpuFrameCore;
class GpuFramePool;
}  // namespace mkvc::gpu

namespace mkvc::gpu::nvidia {

class NvdecApi;

/**
 * @brief Consume a mapped NVDEC NV12 frame into a pooled CUDA frame lease.
 *
 * @param api Loaded CUDA/NVCUVID function table.
 * @param decoder Decoder that owns the mapping.
 * @param context CUDA context associated with the mapped pointer.
 * @param device_pointer Mapped luma-plane device address.
 * @param pitch Device row pitch shared by luma and chroma planes.
 * @param width Visible frame width.
 * @param height Visible frame height.
 * @param pts_ns Presentation timestamp in nanoseconds.
 * @param pool Fixed-capacity pool that bounds outstanding mapped frames.
 * @param release_mapping Callback invoked when the returned lease is released.
 * @param frame Receives the pooled frame core only on success.
 * @param error Receives native-handle or pool diagnostics.
 * @return MKVC_OK, or the native-handle/pool error result.
 *
 * Ownership of the NVDEC mapping transfers to `release_mapping` on success. On
 * failure this function unmaps it before returning. No pixel data is copied.
 */
mkvc_result acquire_nvdec_gpu_frame(NvdecApi& api, CUvideodecoder decoder, CUcontext context,
                                    unsigned long long device_pointer, unsigned int pitch,
                                    uint32_t width, uint32_t height, int64_t pts_ns,
                                    const std::shared_ptr<GpuFramePool>& pool,
                                    std::function<void()> release_mapping,
                                    std::shared_ptr<GpuFrameCore>& frame, std::string& error);

}  // namespace mkvc::gpu::nvidia
