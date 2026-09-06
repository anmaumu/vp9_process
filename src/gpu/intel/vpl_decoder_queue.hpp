#pragma once

#include <vpl/mfxvideo.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc {
struct DecodedFrame;
namespace gpu {
class GpuFrameCore;
class GpuFramePool;
namespace intel {
class VplSession;
}
}  // namespace gpu
}  // namespace mkvc

namespace mkvc::gpu::intel {

/**
 * @brief Own ordered oneVPL decode submissions and output surface leases.
 *
 * CPU collection waits, maps, converts NV12 to I420, and releases the oneVPL
 * surface. GPU collection transfers the referenced surface into GpuFrameCore;
 * the surface and session then remain alive until the returned frame is released.
 */
class VplDecoderQueue {
   public:
    VplDecoderQueue(mfxSession session, uint32_t async_depth, std::shared_ptr<VplSession> lifetime,
                    std::shared_ptr<GpuFramePool> gpu_pool = {});
    ~VplDecoderQueue();
    VplDecoderQueue(const VplDecoderQueue&) = delete;
    VplDecoderQueue& operator=(const VplDecoderQueue&) = delete;

    /** Submit compressed input, or nullptr while draining delayed output. */
    mkvc_result submit(mfxBitstream* bitstream, std::string& error);
    /** Wait for and convert the oldest output into an owned CPU frame. */
    mkvc_result collect_cpu(std::vector<std::unique_ptr<DecodedFrame>>& frames, std::string& error);
    /** Transfer the oldest output into a leased GPU frame. */
    mkvc_result collect_gpu(std::vector<std::shared_ptr<GpuFrameCore>>& frames, std::string& error);
    /** Drain every delayed CPU frame in FIFO order. */
    mkvc_result drain_cpu(std::vector<std::unique_ptr<DecodedFrame>>& frames, std::string& error);
    /** Drain every delayed GPU frame in FIFO order. */
    mkvc_result drain_gpu(std::vector<std::shared_ptr<GpuFrameCore>>& frames, std::string& error);
    /** Synchronize and release every pending surface without emitting frames. */
    void close() noexcept;

    /** Number of output surfaces currently waiting for collection. */
    size_t pending_count() const noexcept;
    /** Configured maximum asynchronous decode depth. */
    uint32_t async_depth() const noexcept;
    /** Largest pending queue depth observed since construction. */
    uint32_t max_pending_observed() const noexcept;
#if defined(MKVC_ENABLE_TEST_HOOKS)
    /** Test-only hook: inject device loss after N successful collections. */
    void set_test_device_loss_after(uint32_t completed_syncpoints) noexcept;
#endif

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc::gpu::intel
