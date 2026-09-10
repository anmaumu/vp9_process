/**
 * @file vpl_decoder_queue_state.hpp
 * @brief Mutable state shared by oneVPL decoder queue implementation units.
 */
#pragma once

#include <atomic>
#include <deque>
#include <limits>
#include <memory>

#include "vpl_decoder_gpu_output.hpp"
#include "vpl_decoder_queue.hpp"

namespace mkvc::gpu::intel {

/** Internal FIFO, output owner and metrics for one decoder queue. */
struct VplDecoderQueue::Impl {
    /** One submitted surface waiting for ordered collection. */
    struct Pending {
        mfxFrameSurface1* surface = nullptr;
        mfxSyncPoint sync = nullptr;
    };

    mfxSession session = nullptr;
    uint32_t async_depth = 0;
    std::deque<Pending> pending;
    std::unique_ptr<VplDecoderGpuOutput> gpu_output;
    std::atomic<uint32_t> max_pending{0};
#if defined(MKVC_ENABLE_TEST_HOOKS)
    uint32_t test_device_loss_after = std::numeric_limits<uint32_t>::max();
    uint32_t collected_syncpoints = 0;
#endif
    bool closed = false;
};

}  // namespace mkvc::gpu::intel
