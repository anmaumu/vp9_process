#include <thread>

#include "vpl_decoder_queue.hpp"
#include "vpl_decoder_queue_state.hpp"

namespace mkvc::gpu::intel {

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

}  // namespace mkvc::gpu::intel
