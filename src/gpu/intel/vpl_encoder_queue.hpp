#pragma once

#include <vpl/mfxvideo.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc {
struct IntelEncodedPacket;
namespace gpu {
class GpuFrameCore;
class ManualCompletion;
}  // namespace gpu
}  // namespace mkvc

namespace mkvc::gpu::intel {

/**
 * @brief Own ordered oneVPL encode submissions and their completion leases.
 *
 * The queue keeps bitstream buffers alive through SyncOperation, collects only
 * from the front to preserve packet order, and resolves each input completion
 * exactly when oneVPL no longer needs the submitted surface.
 */
class VplEncoderQueue {
   public:
    VplEncoderQueue(mfxSession session, uint32_t codec, uint32_t fps_num, uint32_t fps_den,
                    size_t bitstream_capacity, uint32_t async_depth);
    ~VplEncoderQueue();
    VplEncoderQueue(const VplEncoderQueue&) = delete;
    VplEncoderQueue& operator=(const VplEncoderQueue&) = delete;

    /** Submit one surface, or nullptr while draining delayed encoder output. */
    mkvc_result submit(mfxFrameSurface1* surface, std::string& error,
                       std::shared_ptr<ManualCompletion> completion = {},
                       std::weak_ptr<GpuFrameCore> input_frame = {});
    /** Wait for and remove the oldest ordered submission. */
    mkvc_result collect_oldest(std::vector<IntelEncodedPacket>& packets, std::string& error);
    /** Submit drain markers and collect every remaining ordered packet. */
    mkvc_result drain(std::vector<IntelEncodedPacket>& packets, std::string& error);
    /** Resolve outstanding completion leases without emitting packets. */
    void close() noexcept;

    /** Number of submissions currently waiting on a SyncPoint. */
    size_t pending_count() const noexcept;
    /** Configured maximum number of asynchronous submissions. */
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
