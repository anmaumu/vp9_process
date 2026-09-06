#include "vpl_decoder_queue.hpp"

#include <atomic>
#include <deque>
#include <limits>
#include <thread>
#include <utility>

#include "../../intel_vpl_decoder.hpp"
#include "vpl_decoder_cpu_output.hpp"
#include "vpl_decoder_gpu_output.hpp"
#include "vpl_session.hpp"

namespace mkvc::gpu::intel {
namespace {

constexpr mfxU32 kSyncWaitMs = 100;

template <typename Frames, typename Collector>
mkvc_result drain_queue(VplDecoderQueue& queue, Frames& frames, Collector collect,
                        std::string& error) {
    while (true) {
        mkvc_result result = queue.submit(nullptr, error);
        if (result == MKVC_END_OF_STREAM) break;
        if (result == MKVC_WOULD_BLOCK && queue.pending_count() != 0) {
            result = collect(frames, error);
            if (result == MKVC_OK) continue;
        }
        if (result != MKVC_OK) return result;
        if (queue.pending_count() >= queue.async_depth()) {
            result = collect(frames, error);
            if (result != MKVC_OK) return result;
        }
    }
    while (queue.pending_count() != 0) {
        const mkvc_result result = collect(frames, error);
        if (result != MKVC_OK) return result;
    }
    return MKVC_OK;
}

}  // namespace

struct VplDecoderQueue::Impl {
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

VplDecoderQueue::VplDecoderQueue(mfxSession session, uint32_t async_depth,
                                 std::shared_ptr<VplSession> lifetime,
                                 std::shared_ptr<GpuFramePool> gpu_pool)
    : impl_(std::make_unique<Impl>()) {
    impl_->session = session;
    impl_->async_depth = async_depth;
    impl_->gpu_output =
        std::make_unique<VplDecoderGpuOutput>(session, std::move(lifetime), std::move(gpu_pool));
}

VplDecoderQueue::~VplDecoderQueue() { close(); }

mkvc_result VplDecoderQueue::submit(mfxBitstream* bitstream, std::string& error) {
    if (impl_->closed) {
        error = "oneVPL decoder queue is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    mfxFrameSurface1* surface = nullptr;
    mfxSyncPoint sync = nullptr;
    mfxStatus status;
    do {
        status =
            MFXVideoDECODE_DecodeFrameAsync(impl_->session, bitstream, nullptr, &surface, &sync);
        if (status == MFX_WRN_DEVICE_BUSY) std::this_thread::yield();
    } while (status == MFX_WRN_DEVICE_BUSY);
    if (status == MFX_ERR_MORE_DATA) return MKVC_END_OF_STREAM;
    if (status == MFX_ERR_MORE_SURFACE) return MKVC_WOULD_BLOCK;
    if (status < MFX_ERR_NONE) {
        error = "oneVPL DecodeFrameAsync failed with status " + std::to_string(status);
        return status == MFX_ERR_DEVICE_LOST ? MKVC_ERROR_IO : MKVC_ERROR_CODEC;
    }
    if (sync != nullptr && surface != nullptr) {
        impl_->pending.push_back({surface, sync});
        const uint32_t observed = static_cast<uint32_t>(impl_->pending.size());
        uint32_t current = impl_->max_pending.load(std::memory_order_relaxed);
        while (current < observed && !impl_->max_pending.compare_exchange_weak(
                                         current, observed, std::memory_order_relaxed)) {
        }
    } else if (surface != nullptr) {
        surface->FrameInterface->Release(surface);
    }
    return MKVC_OK;
}

mkvc_result VplDecoderQueue::collect_cpu(std::vector<std::unique_ptr<DecodedFrame>>& frames,
                                         std::string& error) {
    if (impl_->pending.empty()) return MKVC_OK;
    const auto pending = impl_->pending.front();
    impl_->pending.pop_front();
#if defined(MKVC_ENABLE_TEST_HOOKS)
    if (impl_->collected_syncpoints >= impl_->test_device_loss_after) {
        mfxStatus status;
        do {
            status = MFXVideoCORE_SyncOperation(impl_->session, pending.sync, kSyncWaitMs);
        } while (status == MFX_WRN_IN_EXECUTION);
        pending.surface->FrameInterface->Release(pending.surface);
        error = "injected Intel decoder device loss";
        return MKVC_ERROR_IO;
    }
#endif
    mfxStatus status;
    do {
        status = MFXVideoCORE_SyncOperation(impl_->session, pending.sync, kSyncWaitMs);
    } while (status == MFX_WRN_IN_EXECUTION);
    if (status != MFX_ERR_NONE) {
        pending.surface->FrameInterface->Release(pending.surface);
        error = "oneVPL decoder SyncOperation failed with status " + std::to_string(status);
        return status == MFX_ERR_DEVICE_LOST ? MKVC_ERROR_IO : MKVC_ERROR_CODEC;
    }
    std::unique_ptr<DecodedFrame> frame;
    const mkvc_result result = copy_vpl_surface_to_i420(pending.surface, frame, error);
    pending.surface->FrameInterface->Release(pending.surface);
    if (result == MKVC_OK) frames.push_back(std::move(frame));
#if defined(MKVC_ENABLE_TEST_HOOKS)
    if (result == MKVC_OK) ++impl_->collected_syncpoints;
#endif
    return result;
}

mkvc_result VplDecoderQueue::collect_gpu(std::vector<std::shared_ptr<GpuFrameCore>>& frames,
                                         std::string& error) {
    if (impl_->pending.empty()) return MKVC_OK;
    if (!impl_->gpu_output->has_capacity()) {
        error = "Intel GPU frame pool is full";
        return MKVC_WOULD_BLOCK;
    }
    const auto pending = impl_->pending.front();
#if defined(MKVC_ENABLE_TEST_HOOKS)
    if (impl_->collected_syncpoints >= impl_->test_device_loss_after) {
        mfxStatus status;
        do {
            status = MFXVideoCORE_SyncOperation(impl_->session, pending.sync, kSyncWaitMs);
        } while (status == MFX_WRN_IN_EXECUTION);
        impl_->pending.pop_front();
        pending.surface->FrameInterface->Release(pending.surface);
        error = "injected Intel decoder device loss";
        return MKVC_ERROR_IO;
    }
#endif
    const mkvc_result result =
        impl_->gpu_output->wrap(pending.surface, pending.sync, frames, error);
    if (result == MKVC_WOULD_BLOCK) return result;
    impl_->pending.pop_front();
    if (result != MKVC_OK) {
        pending.surface->FrameInterface->Release(pending.surface);
        return result;
    }
#if defined(MKVC_ENABLE_TEST_HOOKS)
    ++impl_->collected_syncpoints;
#endif
    return MKVC_OK;
}

mkvc_result VplDecoderQueue::drain_cpu(std::vector<std::unique_ptr<DecodedFrame>>& frames,
                                       std::string& error) {
    return drain_queue(
        *this, frames,
        [this](auto& output, auto& diagnostic) { return collect_cpu(output, diagnostic); }, error);
}

mkvc_result VplDecoderQueue::drain_gpu(std::vector<std::shared_ptr<GpuFrameCore>>& frames,
                                       std::string& error) {
    return drain_queue(
        *this, frames,
        [this](auto& output, auto& diagnostic) { return collect_gpu(output, diagnostic); }, error);
}

void VplDecoderQueue::close() noexcept {
    if (impl_->closed) return;
    for (const auto& pending : impl_->pending) {
        if (pending.sync != nullptr) {
            mfxStatus status;
            do {
                status = MFXVideoCORE_SyncOperation(impl_->session, pending.sync, kSyncWaitMs);
            } while (status == MFX_WRN_IN_EXECUTION);
        }
        if (pending.surface != nullptr) pending.surface->FrameInterface->Release(pending.surface);
    }
    impl_->pending.clear();
    impl_->gpu_output->close();
    impl_->closed = true;
}

size_t VplDecoderQueue::pending_count() const noexcept { return impl_->pending.size(); }

uint32_t VplDecoderQueue::async_depth() const noexcept { return impl_->async_depth; }

uint32_t VplDecoderQueue::max_pending_observed() const noexcept {
    return impl_->max_pending.load(std::memory_order_relaxed);
}

#if defined(MKVC_ENABLE_TEST_HOOKS)
void VplDecoderQueue::set_test_device_loss_after(uint32_t completed_syncpoints) noexcept {
    impl_->test_device_loss_after = completed_syncpoints;
}
#endif

}  // namespace mkvc::gpu::intel
