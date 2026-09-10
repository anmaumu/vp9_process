#include "vpl_decoder_queue.hpp"
#include "vpl_decoder_queue_state.hpp"

namespace mkvc::gpu::intel {
namespace {

constexpr mfxU32 kSyncWaitMs = 100;

}  // namespace

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

}  // namespace mkvc::gpu::intel
