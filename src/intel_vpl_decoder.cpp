#include "intel_vpl_decoder.hpp"

#include "gpu/intel/test_render_node_filter.hpp"
#if defined(MKVC_HAS_INTEL_ONEVPL)
#include "gpu/gpu_frame_pool.hpp"
#include "gpu/intel/vpl_decoder_queue.hpp"
#include "gpu/intel/vpl_session.hpp"
#endif

#if defined(MKVC_HAS_INTEL_ONEVPL)
#include <vpl/mfxdispatcher.h>
#include <vpl/mfxvideo.h>
#endif

#include <limits>
#include <utility>

namespace mkvc {

struct IntelVplDecoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    mfxLoader loader = nullptr;
    mfxSession session = nullptr;
    mfxVideoParam parameters{};
    mfxBitstream bitstream{};
    std::vector<uint8_t> packet;
    std::shared_ptr<gpu::intel::VplSession> lifetime;
    std::unique_ptr<gpu::intel::VplDecoderQueue> queue;
#endif
    uint32_t codec = 0;
    bool gpu_output = false;
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

mfxU32 vpl_codec(uint32_t codec) { return codec == MKVC_CODEC_VP9 ? MFX_CODEC_VP9 : MFX_CODEC_AV1; }

}  // namespace
#endif

std::unique_ptr<IntelVplDecoder> IntelVplDecoder::create(uint32_t codec, std::string& error,
                                                         uint32_t async_depth, bool gpu_output) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)codec;
    (void)async_depth;
    (void)gpu_output;
    error = "Intel oneVPL backend was not built";
    return nullptr;
#else
    if ((codec != MKVC_CODEC_VP9 && codec != MKVC_CODEC_AV1) || async_depth == 0 ||
        async_depth > 8) {
        error = "invalid oneVPL decoder codec";
        return nullptr;
    }
    auto decoder = std::unique_ptr<IntelVplDecoder>(new IntelVplDecoder());
    auto& impl = *decoder->impl_;
    impl.codec = codec;
    impl.gpu_output = gpu_output;
    std::shared_ptr<gpu::GpuFramePool> gpu_pool;
    if (gpu_output) gpu_pool = std::make_shared<gpu::GpuFramePool>(async_depth + 2);
    impl.lifetime = std::make_shared<gpu::intel::VplSession>();
    if (!impl.lifetime->load()) {
        error = "MFXLoad failed";
        return nullptr;
    }
    impl.loader = impl.lifetime->loader();
    mfxConfig hardware = MFXCreateConfig(impl.loader);
    mfxConfig codec_filter = MFXCreateConfig(impl.loader);
    if (hardware == nullptr || codec_filter == nullptr) {
        error = "MFXCreateConfig failed";
        return nullptr;
    }
    mfxVariant value{};
    value.Type = MFX_VARIANT_TYPE_U32;
    value.Data.U32 = MFX_IMPL_TYPE_HARDWARE;
    if (MFXSetConfigFilterProperty(hardware,
                                   reinterpret_cast<const mfxU8*>("mfxImplDescription.Impl"),
                                   value) != MFX_ERR_NONE) {
        error = "oneVPL hardware decoder filter failed";
        return nullptr;
    }
    value.Data.U32 = vpl_codec(codec);
#if defined(MKVC_ENABLE_TEST_HOOKS) && defined(__linux__)
    if (!gpu::intel::test_render_node_filter(impl.loader)) {
        error = "invalid or unsupported test Intel render-node filter";
        return nullptr;
    }
#endif
    if (MFXSetConfigFilterProperty(codec_filter,
                                   reinterpret_cast<const mfxU8*>(
                                       "mfxImplDescription.mfxDecoderDescription.decoder.CodecID"),
                                   value) != MFX_ERR_NONE ||
        impl.lifetime->create_session() != MFX_ERR_NONE) {
        error = "no matching Intel hardware decoder is available";
        return nullptr;
    }
    impl.session = impl.lifetime->session();
    impl.queue = std::make_unique<gpu::intel::VplDecoderQueue>(impl.session, async_depth,
                                                               impl.lifetime, std::move(gpu_pool));
    return decoder;
#endif
}

mkvc_result IntelVplDecoder::decode(const uint8_t* data, size_t size, int64_t pts,
                                    std::vector<std::unique_ptr<DecodedFrame>>& frames,
                                    std::string& error) {
    frames.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)data;
    (void)size;
    (void)pts;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained) {
        error = "oneVPL decoder is closed or drained";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (data == nullptr || size == 0 || size > std::numeric_limits<mfxU32>::max()) {
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
        impl.parameters.IOPattern =
            impl.gpu_output ? MFX_IOPATTERN_OUT_VIDEO_MEMORY : MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
        const mfxStatus header =
            MFXVideoDECODE_DecodeHeader(impl.session, &impl.bitstream, &impl.parameters);
        if (header != MFX_ERR_NONE) {
            error = "oneVPL DecodeHeader failed with status " + std::to_string(header);
            return header == MFX_ERR_MORE_DATA ? MKVC_ERROR_CODEC : MKVC_ERROR_NOT_SUPPORTED;
        }
        impl.parameters.AsyncDepth = static_cast<mfxU16>(impl.queue->async_depth());
        const mfxStatus initialized = MFXVideoDECODE_Init(impl.session, &impl.parameters);
        if (initialized < MFX_ERR_NONE) {
            error = "oneVPL decoder Init failed with status " + std::to_string(initialized);
            return MKVC_ERROR_CODEC;
        }
        impl.decoder_initialized = true;
        impl.lifetime->mark_initialized(gpu::intel::VplSession::Component::kDecode);
    }
    while (impl.bitstream.DataLength > 0) {
        const mfxU32 before = impl.bitstream.DataLength;
        mkvc_result result = impl.queue->submit(&impl.bitstream, error);
        if (result == MKVC_WOULD_BLOCK && impl.queue->pending_count() != 0) {
            result = impl.queue->collect_cpu(frames, error);
            if (result == MKVC_OK) continue;
        }
        if (result != MKVC_OK && result != MKVC_END_OF_STREAM) return result;
        if (result == MKVC_OK && impl.queue->pending_count() >= impl.queue->async_depth()) {
            result = impl.queue->collect_cpu(frames, error);
            if (result != MKVC_OK) return result;
        }
        if (impl.bitstream.DataLength >= before) break;
    }
    return MKVC_OK;
#endif
}

mkvc_result IntelVplDecoder::drain(std::vector<std::unique_ptr<DecodedFrame>>& frames,
                                   std::string& error) {
    frames.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained || !impl.decoder_initialized) return MKVC_OK;
    const mkvc_result result = impl.queue->drain_cpu(frames, error);
    if (result != MKVC_OK) return result;
    impl.drained = true;
    return MKVC_OK;
#endif
}

mkvc_result IntelVplDecoder::decode_gpu(const uint8_t* data, size_t size, int64_t pts,
                                        std::vector<std::shared_ptr<gpu::GpuFrameCore>>& frames,
                                        std::string& error) {
    frames.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)data;
    (void)size;
    (void)pts;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (!impl.gpu_output) {
        error = "Intel decoder was not created for GPU output";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (impl.closed || impl.drained) {
        error = "oneVPL decoder is closed or drained";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (data == nullptr || size == 0 || size > std::numeric_limits<mfxU32>::max()) {
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
        impl.parameters.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
        const mfxStatus header =
            MFXVideoDECODE_DecodeHeader(impl.session, &impl.bitstream, &impl.parameters);
        if (header != MFX_ERR_NONE) {
            error = "oneVPL DecodeHeader failed with status " + std::to_string(header);
            return header == MFX_ERR_MORE_DATA ? MKVC_ERROR_CODEC : MKVC_ERROR_NOT_SUPPORTED;
        }
        impl.parameters.AsyncDepth = static_cast<mfxU16>(impl.queue->async_depth());
        const mfxStatus initialized = MFXVideoDECODE_Init(impl.session, &impl.parameters);
        if (initialized < MFX_ERR_NONE) {
            error = "oneVPL video-memory decoder Init failed with status " +
                    std::to_string(initialized);
            return MKVC_ERROR_CODEC;
        }
        impl.decoder_initialized = true;
        impl.lifetime->mark_initialized(gpu::intel::VplSession::Component::kDecode);
    }
    while (impl.bitstream.DataLength > 0) {
        const mfxU32 before = impl.bitstream.DataLength;
        mkvc_result result = impl.queue->submit(&impl.bitstream, error);
        if (result == MKVC_WOULD_BLOCK && impl.queue->pending_count() != 0) {
            result = impl.queue->collect_gpu(frames, error);
            if (result == MKVC_OK) continue;
        }
        if (result != MKVC_OK && result != MKVC_END_OF_STREAM) return result;
        if (result == MKVC_OK && impl.queue->pending_count() >= impl.queue->async_depth()) {
            result = impl.queue->collect_gpu(frames, error);
            if (result != MKVC_OK) return result;
        }
        if (impl.bitstream.DataLength >= before) break;
    }
    return MKVC_OK;
#endif
}

mkvc_result IntelVplDecoder::drain_gpu(std::vector<std::shared_ptr<gpu::GpuFrameCore>>& frames,
                                       std::string& error) {
    frames.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (!impl.gpu_output) {
        error = "Intel decoder was not created for GPU output";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (impl.closed || impl.drained || !impl.decoder_initialized) return MKVC_OK;
    const mkvc_result result = impl.queue->drain_gpu(frames, error);
    if (result != MKVC_OK) return result;
    impl.drained = true;
    return MKVC_OK;
#endif
}

mkvc_result IntelVplDecoder::close(std::string& error) {
    (void)error;
    if (impl_->closed) return MKVC_OK;
#if defined(MKVC_HAS_INTEL_ONEVPL)
    if (impl_->queue) impl_->queue->close();
    impl_->decoder_initialized = false;
    impl_->lifetime.reset();
    impl_->session = nullptr;
    impl_->loader = nullptr;
#endif
    impl_->closed = true;
    return MKVC_OK;
}

uint32_t IntelVplDecoder::max_pending_observed() const {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    return impl_->queue ? impl_->queue->max_pending_observed() : 0;
#else
    return 0;
#endif
}

#if defined(MKVC_ENABLE_TEST_HOOKS)
void IntelVplDecoder::set_test_device_loss_after(uint32_t completed_syncpoints) {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    if (impl_->queue) impl_->queue->set_test_device_loss_after(completed_syncpoints);
#else
    (void)completed_syncpoints;
#endif
}
#endif

}  // namespace mkvc
