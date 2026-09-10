#include <utility>

#include "../../intel_vpl_decoder.hpp"
#include "vpl_decoder_cpu_output.hpp"
#include "vpl_decoder_queue.hpp"
#include "vpl_decoder_queue_state.hpp"

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

}  // namespace mkvc::gpu::intel
