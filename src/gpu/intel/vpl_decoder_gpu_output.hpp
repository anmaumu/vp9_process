/**
 * @file vpl_decoder_gpu_output.hpp
 * @brief oneVPL decoded-surface GPU lease creation and pool backpressure.
 */
#pragma once

#include <vpl/mfxvideo.h>

#include <memory>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu {
class GpuFrameCore;
class GpuFramePool;
namespace intel {
class VplSession;

/**
 * @brief Convert decoded oneVPL surfaces into bounded GPU frame leases.
 *
 * A successful wrap transfers surface-release responsibility to the returned
 * GpuFrameCore. WOULD_BLOCK and error results leave ownership with the caller.
 */
class VplDecoderGpuOutput final {
   public:
    VplDecoderGpuOutput(mfxSession session, std::shared_ptr<VplSession> lifetime,
                        std::shared_ptr<GpuFramePool> pool);
    ~VplDecoderGpuOutput() = default;
    VplDecoderGpuOutput(const VplDecoderGpuOutput&) = delete;
    VplDecoderGpuOutput& operator=(const VplDecoderGpuOutput&) = delete;

    /** Return whether another exported GPU frame lease can be acquired. */
    bool has_capacity() const noexcept;

    /** Wrap one pending surface and append its GPU frame lease. */
    mkvc_result wrap(mfxFrameSurface1* surface, mfxSyncPoint sync,
                     std::vector<std::shared_ptr<GpuFrameCore>>& frames, std::string& error);

    /** Release queue-owned pool and session references. */
    void close() noexcept;

   private:
    mfxSession session_ = nullptr;
    std::shared_ptr<VplSession> lifetime_;
    std::shared_ptr<GpuFramePool> pool_;
};

}  // namespace intel
}  // namespace mkvc::gpu
