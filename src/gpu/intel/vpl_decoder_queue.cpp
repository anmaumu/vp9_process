#include "vpl_decoder_queue.hpp"

#include <libyuv/convert.h>

#include <atomic>
#include <deque>
#include <limits>
#include <thread>
#include <utility>

#include "../../intel_vpl_decoder.hpp"
#include "../gpu_frame_pool.hpp"
#include "intel_surface_factory.hpp"
#include "vpl_session.hpp"

namespace mkvc::gpu::intel {
namespace {

constexpr mfxU32 kSyncWaitMs = 100;

mkvc_result copy_surface(mfxFrameSurface1* surface, std::unique_ptr<DecodedFrame>& frame,
                         std::string& error) {
    if (surface == nullptr || surface->FrameInterface == nullptr) {
        error = "oneVPL returned an invalid decoded surface";
        return MKVC_ERROR_CODEC;
    }
    mfxStatus status = surface->FrameInterface->Map(surface, MFX_MAP_READ);
    if (status != MFX_ERR_NONE) {
        error = "oneVPL failed to map a decoded surface";
        return MKVC_ERROR_CODEC;
    }
    const auto& info = surface->Info;
    const uint32_t width = info.CropW;
    const uint32_t height = info.CropH;
    const uint32_t pitch =
        (static_cast<uint32_t>(surface->Data.PitchHigh) << 16) | surface->Data.PitchLow;
    if (info.FourCC != MFX_FOURCC_NV12 || width == 0 || height == 0 || (width & 1u) != 0 ||
        (height & 1u) != 0 || width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        surface->FrameInterface->Unmap(surface);
        error = "oneVPL decoder produced an unsupported surface format";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    auto output = std::make_unique<DecodedFrame>();
    output->width = width;
    output->height = height;
    output->pts_ns = static_cast<int64_t>(surface->Data.TimeStamp);
    const size_t y_size = static_cast<size_t>(width) * height;
    const size_t uv_size = static_cast<size_t>(width / 2) * (height / 2);
    output->pixels.resize(y_size + 2 * uv_size);
    output->offsets = {0, y_size, y_size + uv_size};
    output->strides = {static_cast<int32_t>(width), static_cast<int32_t>(width / 2),
                       static_cast<int32_t>(width / 2)};
    const uint8_t* source_y =
        surface->Data.Y + static_cast<size_t>(info.CropY) * pitch + info.CropX;
    const uint8_t* source_uv =
        surface->Data.UV + static_cast<size_t>(info.CropY / 2) * pitch + info.CropX;
    const int conversion =
        libyuv::NV12ToI420(source_y, static_cast<int>(pitch), source_uv, static_cast<int>(pitch),
                           output->pixels.data() + output->offsets[0], output->strides[0],
                           output->pixels.data() + output->offsets[1], output->strides[1],
                           output->pixels.data() + output->offsets[2], output->strides[2],
                           static_cast<int>(width), static_cast<int>(height));
    status = surface->FrameInterface->Unmap(surface);
    if (conversion != 0 || status != MFX_ERR_NONE) {
        error = conversion != 0 ? "libyuv failed to convert decoded NV12"
                                : "oneVPL failed to unmap a decoded surface";
        return conversion != 0 ? MKVC_ERROR_INTERNAL : MKVC_ERROR_CODEC;
    }
    frame = std::move(output);
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
    std::shared_ptr<VplSession> lifetime;
    std::shared_ptr<GpuFramePool> gpu_pool;
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
    impl_->lifetime = std::move(lifetime);
    impl_->gpu_pool = std::move(gpu_pool);
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
    const mkvc_result result = copy_surface(pending.surface, frame, error);
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
    if (!impl_->gpu_pool || impl_->gpu_pool->in_use() >= impl_->gpu_pool->capacity()) {
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
    GpuFramePool::Acquisition acquisition;
    const mkvc_result result =
        wrap_vpl_surface(impl_->session, pending.surface, pending.sync, 0, impl_->lifetime,
                         impl_->gpu_pool, acquisition, error);
    if (result == MKVC_WOULD_BLOCK) return result;
    impl_->pending.pop_front();
    if (result != MKVC_OK) {
        pending.surface->FrameInterface->Release(pending.surface);
        return result;
    }
    frames.push_back(std::move(acquisition.core));
#if defined(MKVC_ENABLE_TEST_HOOKS)
    ++impl_->collected_syncpoints;
#endif
    return MKVC_OK;
}

mkvc_result VplDecoderQueue::drain_cpu(std::vector<std::unique_ptr<DecodedFrame>>& frames,
                                       std::string& error) {
    while (true) {
        mkvc_result result = submit(nullptr, error);
        if (result == MKVC_END_OF_STREAM) break;
        if (result == MKVC_WOULD_BLOCK && pending_count() != 0) {
            result = collect_cpu(frames, error);
            if (result == MKVC_OK) continue;
        }
        if (result != MKVC_OK) return result;
        if (pending_count() >= async_depth()) {
            result = collect_cpu(frames, error);
            if (result != MKVC_OK) return result;
        }
    }
    while (pending_count() != 0) {
        const mkvc_result result = collect_cpu(frames, error);
        if (result != MKVC_OK) return result;
    }
    return MKVC_OK;
}

mkvc_result VplDecoderQueue::drain_gpu(std::vector<std::shared_ptr<GpuFrameCore>>& frames,
                                       std::string& error) {
    while (true) {
        mkvc_result result = submit(nullptr, error);
        if (result == MKVC_END_OF_STREAM) break;
        if (result == MKVC_WOULD_BLOCK && pending_count() != 0) {
            result = collect_gpu(frames, error);
            if (result == MKVC_OK) continue;
        }
        if (result != MKVC_OK) return result;
        if (pending_count() >= async_depth()) {
            result = collect_gpu(frames, error);
            if (result != MKVC_OK) return result;
        }
    }
    while (pending_count() != 0) {
        const mkvc_result result = collect_gpu(frames, error);
        if (result != MKVC_OK) return result;
    }
    return MKVC_OK;
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
    impl_->gpu_pool.reset();
    impl_->lifetime.reset();
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
