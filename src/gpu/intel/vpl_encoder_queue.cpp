#include "vpl_encoder_queue.hpp"

#include <atomic>
#include <deque>
#include <limits>
#include <thread>
#include <utility>

#include "../gpu_frame.hpp"
#include "vpl_bitstream.hpp"

namespace mkvc::gpu::intel {
namespace {

constexpr mfxU32 kSyncWaitMs = 100;

}  // namespace

struct VplEncoderQueue::Impl {
    struct Pending {
        mfxBitstream bitstream{};
        std::vector<uint8_t> storage;
        mfxSyncPoint sync = nullptr;
        std::shared_ptr<ManualCompletion> input_completion;
        std::weak_ptr<GpuFrameCore> input_frame;
    };

    mfxSession session = nullptr;
    uint32_t codec = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    size_t bitstream_capacity = 0;
    uint32_t async_depth = 0;
    std::deque<std::unique_ptr<Pending>> pending;
    std::atomic<uint32_t> max_pending{0};
#if defined(MKVC_ENABLE_TEST_HOOKS)
    uint32_t test_device_loss_after = std::numeric_limits<uint32_t>::max();
    uint32_t collected_syncpoints = 0;
#endif
    bool closed = false;
};

VplEncoderQueue::VplEncoderQueue(mfxSession session, uint32_t codec, uint32_t fps_num,
                                 uint32_t fps_den, size_t bitstream_capacity, uint32_t async_depth)
    : impl_(std::make_unique<Impl>()) {
    impl_->session = session;
    impl_->codec = codec;
    impl_->fps_num = fps_num;
    impl_->fps_den = fps_den;
    impl_->bitstream_capacity = bitstream_capacity;
    impl_->async_depth = async_depth;
}

VplEncoderQueue::~VplEncoderQueue() { close(); }

mkvc_result VplEncoderQueue::submit(mfxFrameSurface1* surface, std::string& error,
                                    std::shared_ptr<ManualCompletion> completion,
                                    std::weak_ptr<GpuFrameCore> input_frame) {
    if (impl_->closed) {
        error = "oneVPL encoder queue is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    auto pending = std::make_unique<Impl::Pending>();
    pending->storage.resize(impl_->bitstream_capacity);
    pending->bitstream.Data = pending->storage.data();
    pending->bitstream.MaxLength = static_cast<mfxU32>(pending->storage.size());
    pending->input_completion = std::move(completion);
    pending->input_frame = std::move(input_frame);
    mfxStatus status;
    do {
        status = MFXVideoENCODE_EncodeFrameAsync(impl_->session, nullptr, surface,
                                                 &pending->bitstream, &pending->sync);
        if (status == MFX_WRN_DEVICE_BUSY) std::this_thread::yield();
    } while (status == MFX_WRN_DEVICE_BUSY);
    if (status == MFX_ERR_MORE_DATA) return MKVC_END_OF_STREAM;
    if (status == MFX_ERR_NOT_ENOUGH_BUFFER) {
        error = "oneVPL bitstream buffer is too small";
        return MKVC_ERROR_BUFFER_TOO_SMALL;
    }
    if (status < MFX_ERR_NONE) {
        error = "oneVPL EncodeFrameAsync failed with status " + std::to_string(status);
        return status == MFX_ERR_DEVICE_LOST ? MKVC_ERROR_IO : MKVC_ERROR_CODEC;
    }
    if (pending->sync != nullptr) {
        impl_->pending.push_back(std::move(pending));
        const uint32_t observed = static_cast<uint32_t>(impl_->pending.size());
        uint32_t current = impl_->max_pending.load(std::memory_order_relaxed);
        while (current < observed && !impl_->max_pending.compare_exchange_weak(
                                         current, observed, std::memory_order_relaxed)) {
        }
    } else if (pending->input_completion) {
        pending->input_completion->complete();
        if (auto frame = pending->input_frame.lock()) frame->poll_recycle();
    }
    return MKVC_OK;
}

mkvc_result VplEncoderQueue::collect_oldest(std::vector<IntelEncodedPacket>& packets,
                                            std::string& error) {
    if (impl_->pending.empty()) return MKVC_OK;
    auto pending = std::move(impl_->pending.front());
    impl_->pending.pop_front();
#if defined(MKVC_ENABLE_TEST_HOOKS)
    if (impl_->collected_syncpoints >= impl_->test_device_loss_after) {
        mfxStatus status;
        do {
            status = MFXVideoCORE_SyncOperation(impl_->session, pending->sync, kSyncWaitMs);
        } while (status == MFX_WRN_IN_EXECUTION);
        error = "injected Intel encoder device loss";
        if (pending->input_completion) pending->input_completion->fail(error);
        return MKVC_ERROR_IO;
    }
#endif
    mfxStatus status;
    do {
        status = MFXVideoCORE_SyncOperation(impl_->session, pending->sync, kSyncWaitMs);
    } while (status == MFX_WRN_IN_EXECUTION);
    if (status != MFX_ERR_NONE) {
        error = "oneVPL SyncOperation failed with status " + std::to_string(status);
        if (pending->input_completion) pending->input_completion->fail(error);
        return status == MFX_ERR_DEVICE_LOST ? MKVC_ERROR_IO : MKVC_ERROR_CODEC;
    }
    const mkvc_result packet_result = append_vpl_encoded_packet(
        pending->bitstream, impl_->codec, impl_->fps_num, impl_->fps_den, packets, error);
    if (packet_result != MKVC_OK) {
        if (pending->input_completion) pending->input_completion->fail(error);
        return packet_result;
    }
    if (pending->input_completion) pending->input_completion->complete();
    if (auto frame = pending->input_frame.lock()) frame->poll_recycle();
#if defined(MKVC_ENABLE_TEST_HOOKS)
    ++impl_->collected_syncpoints;
#endif
    return MKVC_OK;
}

mkvc_result VplEncoderQueue::drain(std::vector<IntelEncodedPacket>& packets, std::string& error) {
    while (true) {
        const mkvc_result result = submit(nullptr, error);
        if (result == MKVC_END_OF_STREAM) break;
        if (result != MKVC_OK) return result;
        if (pending_count() >= async_depth()) {
            const mkvc_result collected = collect_oldest(packets, error);
            if (collected != MKVC_OK) return collected;
        }
    }
    while (pending_count() != 0) {
        const mkvc_result result = collect_oldest(packets, error);
        if (result != MKVC_OK) return result;
    }
    return MKVC_OK;
}

void VplEncoderQueue::close() noexcept {
    if (impl_->closed) return;
    for (const auto& pending : impl_->pending) {
        if (pending->sync == nullptr) continue;
        mfxStatus status;
        do {
            status = MFXVideoCORE_SyncOperation(impl_->session, pending->sync, kSyncWaitMs);
        } while (status == MFX_WRN_IN_EXECUTION);
        if (pending->input_completion) {
            if (status == MFX_ERR_NONE)
                pending->input_completion->complete();
            else
                pending->input_completion->fail("oneVPL encoder close synchronization failed");
        }
    }
    impl_->pending.clear();
    impl_->closed = true;
}

size_t VplEncoderQueue::pending_count() const noexcept { return impl_->pending.size(); }

uint32_t VplEncoderQueue::async_depth() const noexcept { return impl_->async_depth; }

uint32_t VplEncoderQueue::max_pending_observed() const noexcept {
    return impl_->max_pending.load(std::memory_order_relaxed);
}

#if defined(MKVC_ENABLE_TEST_HOOKS)
void VplEncoderQueue::set_test_device_loss_after(uint32_t completed_syncpoints) noexcept {
    impl_->test_device_loss_after = completed_syncpoints;
}
#endif

}  // namespace mkvc::gpu::intel
