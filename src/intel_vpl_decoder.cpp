#include "intel_vpl_decoder.hpp"

#if defined(MKVC_HAS_INTEL_ONEVPL)
#include <libyuv/convert.h>
#include <vpl/mfxdispatcher.h>
#include <vpl/mfxvideo.h>
#endif

#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <thread>
#include <utility>

namespace mkvc {

struct IntelVplDecoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    struct Pending {
        mfxFrameSurface1* surface = nullptr;
        mfxSyncPoint sync = nullptr;
    };
    mfxLoader loader = nullptr;
    mfxSession session = nullptr;
    mfxVideoParam parameters{};
    mfxBitstream bitstream{};
    std::vector<uint8_t> packet;
    std::deque<Pending> pending;
#endif
    uint32_t codec = 0;
    uint32_t async_depth = 4;
    uint32_t max_pending = 0;
    bool decoder_initialized = false;
    bool drained = false;
    bool closed = false;
};

IntelVplDecoder::IntelVplDecoder() : impl_(std::make_unique<Impl>()) {}
IntelVplDecoder::~IntelVplDecoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_INTEL_ONEVPL)
namespace {

constexpr mfxU32 kSyncWaitMs = 100;

mfxU32 vpl_codec(uint32_t codec) {
    return codec == MKVC_CODEC_VP9 ? MFX_CODEC_VP9 : MFX_CODEC_AV1;
}

mkvc_result copy_surface(mfxFrameSurface1* surface,
                         std::unique_ptr<DecodedFrame>& frame,
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
    const uint32_t pitch = (static_cast<uint32_t>(surface->Data.PitchHigh) << 16) |
                           surface->Data.PitchLow;
    if (info.FourCC != MFX_FOURCC_NV12 || width == 0 || height == 0 ||
        (width & 1u) != 0 || (height & 1u) != 0 ||
        width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
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
    output->strides = {static_cast<int32_t>(width),
                       static_cast<int32_t>(width / 2),
                       static_cast<int32_t>(width / 2)};
    const uint8_t* source_y = surface->Data.Y +
        static_cast<size_t>(info.CropY) * pitch + info.CropX;
    const uint8_t* source_uv = surface->Data.UV +
        static_cast<size_t>(info.CropY / 2) * pitch + info.CropX;
    const int conversion = libyuv::NV12ToI420(
        source_y, static_cast<int>(pitch), source_uv, static_cast<int>(pitch),
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

mkvc_result submit_decode(IntelVplDecoder::Impl& impl, mfxBitstream* bitstream,
                          std::string& error) {
    mfxFrameSurface1* surface = nullptr;
    mfxSyncPoint sync = nullptr;
    mfxStatus status;
    do {
        status = MFXVideoDECODE_DecodeFrameAsync(
            impl.session, bitstream, nullptr, &surface, &sync);
        if (status == MFX_WRN_DEVICE_BUSY) std::this_thread::yield();
    } while (status == MFX_WRN_DEVICE_BUSY);
    if (status == MFX_ERR_MORE_DATA) return MKVC_END_OF_STREAM;
    if (status == MFX_ERR_MORE_SURFACE) return MKVC_WOULD_BLOCK;
    if (status < MFX_ERR_NONE) {
        error = "oneVPL DecodeFrameAsync failed with status " +
                std::to_string(status);
        return status == MFX_ERR_DEVICE_LOST ? MKVC_ERROR_IO : MKVC_ERROR_CODEC;
    }
    if (sync != nullptr && surface != nullptr) {
        impl.pending.push_back({surface, sync});
        impl.max_pending = std::max<uint32_t>(
            impl.max_pending, static_cast<uint32_t>(impl.pending.size()));
    } else if (surface != nullptr) {
        surface->FrameInterface->Release(surface);
    }
    return MKVC_OK;
}

mkvc_result collect_oldest(
    IntelVplDecoder::Impl& impl,
    std::vector<std::unique_ptr<DecodedFrame>>& frames, std::string& error) {
    if (impl.pending.empty()) return MKVC_OK;
    const auto pending = impl.pending.front();
    impl.pending.pop_front();
    mfxStatus status;
    do {
        status = MFXVideoCORE_SyncOperation(
            impl.session, pending.sync, kSyncWaitMs);
    } while (status == MFX_WRN_IN_EXECUTION);
    if (status != MFX_ERR_NONE) {
        pending.surface->FrameInterface->Release(pending.surface);
        error = "oneVPL decoder SyncOperation failed with status " +
                std::to_string(status);
        return status == MFX_ERR_DEVICE_LOST ? MKVC_ERROR_IO : MKVC_ERROR_CODEC;
    }
    std::unique_ptr<DecodedFrame> frame;
    const mkvc_result result = copy_surface(pending.surface, frame, error);
    pending.surface->FrameInterface->Release(pending.surface);
    if (result == MKVC_OK) frames.push_back(std::move(frame));
    return result;
}

}  // namespace
#endif

std::unique_ptr<IntelVplDecoder> IntelVplDecoder::create(
    uint32_t codec, std::string& error, uint32_t async_depth) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)codec; (void)async_depth;
    error = "Intel oneVPL backend was not built";
    return nullptr;
#else
    if ((codec != MKVC_CODEC_VP9 && codec != MKVC_CODEC_AV1) ||
        async_depth == 0 || async_depth > 8) {
        error = "invalid oneVPL decoder codec";
        return nullptr;
    }
    auto decoder = std::unique_ptr<IntelVplDecoder>(new IntelVplDecoder());
    auto& impl = *decoder->impl_;
    impl.codec = codec;
    impl.async_depth = async_depth;
    impl.loader = MFXLoad();
    if (impl.loader == nullptr) {
        error = "MFXLoad failed";
        return nullptr;
    }
    mfxConfig hardware = MFXCreateConfig(impl.loader);
    mfxConfig codec_filter = MFXCreateConfig(impl.loader);
    if (hardware == nullptr || codec_filter == nullptr) {
        error = "MFXCreateConfig failed";
        return nullptr;
    }
    mfxVariant value{};
    value.Type = MFX_VARIANT_TYPE_U32;
    value.Data.U32 = MFX_IMPL_TYPE_HARDWARE;
    if (MFXSetConfigFilterProperty(
            hardware, reinterpret_cast<const mfxU8*>("mfxImplDescription.Impl"),
            value) != MFX_ERR_NONE) {
        error = "oneVPL hardware decoder filter failed";
        return nullptr;
    }
    value.Data.U32 = vpl_codec(codec);
    if (MFXSetConfigFilterProperty(
            codec_filter,
            reinterpret_cast<const mfxU8*>(
                "mfxImplDescription.mfxDecoderDescription.decoder.CodecID"),
            value) != MFX_ERR_NONE ||
        MFXCreateSession(impl.loader, 0, &impl.session) != MFX_ERR_NONE) {
        error = "no matching Intel hardware decoder is available";
        return nullptr;
    }
    return decoder;
#endif
}

mkvc_result IntelVplDecoder::decode(
    const uint8_t* data, size_t size, int64_t pts,
    std::vector<std::unique_ptr<DecodedFrame>>& frames, std::string& error) {
    frames.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)data; (void)size; (void)pts;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained) {
        error = "oneVPL decoder is closed or drained";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (data == nullptr || size == 0 ||
        size > std::numeric_limits<mfxU32>::max()) {
        error = "invalid compressed packet";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    impl.packet.assign(data, data + size);
    impl.bitstream = {};
    impl.bitstream.Data = impl.packet.data();
    impl.bitstream.DataLength = static_cast<mfxU32>(size);
    impl.bitstream.MaxLength = static_cast<mfxU32>(size);
    impl.bitstream.TimeStamp = static_cast<mfxU64>(pts);
    if (!impl.decoder_initialized) {
        impl.parameters = {};
        impl.parameters.mfx.CodecId = vpl_codec(impl.codec);
        impl.parameters.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
        const mfxStatus header = MFXVideoDECODE_DecodeHeader(
            impl.session, &impl.bitstream, &impl.parameters);
        if (header != MFX_ERR_NONE) {
            error = "oneVPL DecodeHeader failed with status " +
                    std::to_string(header);
            return header == MFX_ERR_MORE_DATA ? MKVC_ERROR_CODEC
                                               : MKVC_ERROR_NOT_SUPPORTED;
        }
        impl.parameters.AsyncDepth = static_cast<mfxU16>(impl.async_depth);
        const mfxStatus initialized = MFXVideoDECODE_Init(
            impl.session, &impl.parameters);
        if (initialized < MFX_ERR_NONE) {
            error = "oneVPL decoder Init failed with status " +
                    std::to_string(initialized);
            return MKVC_ERROR_CODEC;
        }
        impl.decoder_initialized = true;
    }
    while (impl.bitstream.DataLength > 0) {
        const mfxU32 before = impl.bitstream.DataLength;
        mkvc_result result = submit_decode(impl, &impl.bitstream, error);
        if (result == MKVC_WOULD_BLOCK && !impl.pending.empty()) {
            result = collect_oldest(impl, frames, error);
            if (result == MKVC_OK) continue;
        }
        if (result != MKVC_OK && result != MKVC_END_OF_STREAM) return result;
        if (result == MKVC_OK && impl.pending.size() >= impl.async_depth) {
            result = collect_oldest(impl, frames, error);
            if (result != MKVC_OK) return result;
        }
        if (impl.bitstream.DataLength >= before) break;
    }
    return MKVC_OK;
#endif
}

mkvc_result IntelVplDecoder::drain(
    std::vector<std::unique_ptr<DecodedFrame>>& frames, std::string& error) {
    frames.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained || !impl.decoder_initialized) return MKVC_OK;
    while (true) {
        mkvc_result result = submit_decode(impl, nullptr, error);
        if (result == MKVC_END_OF_STREAM) break;
        if (result == MKVC_WOULD_BLOCK && !impl.pending.empty()) {
            result = collect_oldest(impl, frames, error);
            if (result == MKVC_OK) continue;
        }
        if (result != MKVC_OK) return result;
        if (impl.pending.size() >= impl.async_depth) {
            result = collect_oldest(impl, frames, error);
            if (result != MKVC_OK) return result;
        }
    }
    while (!impl.pending.empty()) {
        const mkvc_result result = collect_oldest(impl, frames, error);
        if (result != MKVC_OK) return result;
    }
    impl.drained = true;
    return MKVC_OK;
#endif
}

mkvc_result IntelVplDecoder::close(std::string& error) {
    (void)error;
    if (impl_->closed) return MKVC_OK;
#if defined(MKVC_HAS_INTEL_ONEVPL)
    for (const auto& pending : impl_->pending) {
        if (pending.surface != nullptr)
            pending.surface->FrameInterface->Release(pending.surface);
    }
    impl_->pending.clear();
    if (impl_->decoder_initialized) {
        MFXVideoDECODE_Close(impl_->session);
        impl_->decoder_initialized = false;
    }
    if (impl_->session != nullptr) MFXClose(impl_->session);
    if (impl_->loader != nullptr) MFXUnload(impl_->loader);
    impl_->session = nullptr;
    impl_->loader = nullptr;
#endif
    impl_->closed = true;
    return MKVC_OK;
}

uint32_t IntelVplDecoder::max_pending_observed() const {
    return impl_->max_pending;
}

}  // namespace mkvc
