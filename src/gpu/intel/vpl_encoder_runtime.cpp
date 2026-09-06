#include "gpu/intel/vpl_encoder_runtime.hpp"

#include <vpl/mfxdispatcher.h>

#include <algorithm>
#include <limits>
#include <utility>

#include "gpu/gpu_frame.hpp"
#include "gpu/intel/test_render_node_filter.hpp"
#include "gpu/intel/vpl_session.hpp"

#if defined(_WIN32)
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace mkvc::gpu::intel {

class VplEncoderRuntime::State final {
   public:
    VplSession session;
};

namespace {

mfxU32 vpl_codec(uint32_t codec) { return codec == MKVC_CODEC_VP9 ? MFX_CODEC_VP9 : MFX_CODEC_AV1; }

bool configure_external_device(mfxLoader loader, const std::shared_ptr<GpuFrameCore>& owner,
                               mkvc_gpu_frame*& owner_lease, uint64_t& identity,
                               std::string& error) {
    mkvc_gpu_native_handle_desc native{};
    if (owner->get_native_handle(native, error) != MKVC_OK) return false;

    mfxHandleType handle_type;
    mfxHDL device = nullptr;
#if defined(_WIN32)
    Microsoft::WRL::ComPtr<ID3D11Device> texture_device;
    if (native.type != MKVC_GPU_NATIVE_D3D11_TEXTURE || native.handles[0] == 0) {
        error = "external Intel encoder requires a D3D11 texture";
        return false;
    }
    reinterpret_cast<ID3D11Texture2D*>(static_cast<uintptr_t>(native.handles[0]))
        ->GetDevice(texture_device.GetAddressOf());
    device = texture_device.Get();
    handle_type = MFX_HANDLE_D3D11_DEVICE;
#else
    if (native.type != MKVC_GPU_NATIVE_VA_SURFACE || native.handles[0] == 0) {
        error = "external Intel encoder requires a VA display/surface";
        return false;
    }
    device = reinterpret_cast<mfxHDL>(static_cast<uintptr_t>(native.handles[0]));
    handle_type = MFX_HANDLE_VA_DISPLAY;
#endif

    owner_lease = gpu::make_handle(owner);
    identity = reinterpret_cast<uintptr_t>(device);
    mfxConfig type_config = MFXCreateConfig(loader);
    mfxConfig device_config = MFXCreateConfig(loader);
    mfxVariant type_value{};
    type_value.Type = MFX_VARIANT_TYPE_U32;
    type_value.Data.U32 = handle_type;
    mfxVariant device_value{};
    device_value.Type = MFX_VARIANT_TYPE_PTR;
    device_value.Data.Ptr = device;
    if (!device || !type_config || !device_config ||
        MFXSetConfigFilterProperty(type_config, reinterpret_cast<const mfxU8*>("mfxHandleType"),
                                   type_value) != MFX_ERR_NONE ||
        MFXSetConfigFilterProperty(device_config, reinterpret_cast<const mfxU8*>("mfxHDL"),
                                   device_value) != MFX_ERR_NONE) {
        error = "oneVPL external device configuration failed";
        return false;
    }
    return true;
}

void set_encode_parameters(const VplEncoderRuntimeConfig& config, bool video_memory,
                           mfxVideoParam& parameters) {
    parameters.mfx.CodecId = vpl_codec(config.codec);
    parameters.mfx.CodecProfile = static_cast<mfxU16>(
        config.codec == MKVC_CODEC_VP9 ? MFX_PROFILE_VP9_0 : MFX_PROFILE_AV1_MAIN);
    if (config.codec == MKVC_CODEC_AV1) parameters.mfx.CodecLevel = MFX_LEVEL_AV1_63;
    parameters.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    parameters.mfx.RateControlMethod = MFX_RATECONTROL_CQP;
    parameters.mfx.QPI = static_cast<mfxU16>(config.quality);
    parameters.mfx.QPP = static_cast<mfxU16>(config.quality);
    parameters.mfx.QPB = static_cast<mfxU16>(config.quality);
    const uint64_t default_gop =
        std::max<uint64_t>(1, static_cast<uint64_t>(config.fps_num) * 4 / config.fps_den);
    parameters.mfx.GopPicSize = static_cast<mfxU16>(std::min<uint64_t>(
        config.keyframe_interval_frames == 0 ? default_gop : config.keyframe_interval_frames,
        std::numeric_limits<mfxU16>::max()));
    parameters.mfx.GopRefDist = 1;
    parameters.mfx.FrameInfo.FrameRateExtN = config.fps_num;
    parameters.mfx.FrameInfo.FrameRateExtD = config.fps_den;
    parameters.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    parameters.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    parameters.mfx.FrameInfo.CropW = static_cast<mfxU16>(config.width);
    parameters.mfx.FrameInfo.CropH = static_cast<mfxU16>(config.height);
    parameters.mfx.FrameInfo.Width = static_cast<mfxU16>((config.width + 15u) & ~15u);
    parameters.mfx.FrameInfo.Height = static_cast<mfxU16>((config.height + 15u) & ~15u);
    parameters.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    parameters.IOPattern = static_cast<mfxU16>(video_memory ? MFX_IOPATTERN_IN_VIDEO_MEMORY
                                                            : MFX_IOPATTERN_IN_SYSTEM_MEMORY);
    parameters.AsyncDepth = static_cast<mfxU16>(config.async_depth);
}

}  // namespace

VplEncoderRuntime::VplEncoderRuntime() : state_(std::make_unique<State>()) {}

VplEncoderRuntime::~VplEncoderRuntime() {
    state_.reset();
    mkvc_gpu_frame_release(external_device_owner_);
}

std::unique_ptr<VplEncoderRuntime> VplEncoderRuntime::create(
    const VplEncoderRuntimeConfig& config,
    const std::shared_ptr<GpuFrameCore>& external_device_owner, size_t& bitstream_capacity,
    std::string& error) {
    auto runtime = std::unique_ptr<VplEncoderRuntime>(new VplEncoderRuntime());
    if (!runtime->state_->session.load()) {
        error = "MFXLoad failed";
        return nullptr;
    }
    mfxLoader loader = runtime->state_->session.loader();
    mfxConfig hardware = MFXCreateConfig(loader);
    mfxConfig codec_filter = MFXCreateConfig(loader);
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
        error = "oneVPL hardware filter failed";
        return nullptr;
    }
    value.Data.U32 = vpl_codec(config.codec);
    if (MFXSetConfigFilterProperty(codec_filter,
                                   reinterpret_cast<const mfxU8*>(
                                       "mfxImplDescription.mfxEncoderDescription.encoder.CodecID"),
                                   value) != MFX_ERR_NONE) {
        error = "oneVPL encoder codec filter failed";
        return nullptr;
    }
    if (external_device_owner &&
        !configure_external_device(loader, external_device_owner, runtime->external_device_owner_,
                                   runtime->external_device_identity_, error)) {
        return nullptr;
    }
#if defined(MKVC_ENABLE_TEST_HOOKS) && defined(__linux__)
    if (!test_render_node_filter(loader)) {
        error = "invalid or unsupported test Intel render-node filter";
        return nullptr;
    }
#endif
    if (runtime->state_->session.create_session() != MFX_ERR_NONE) {
        error = "no matching Intel hardware encoder is available";
        return nullptr;
    }

    mfxVideoParam parameters{};
    set_encode_parameters(config, external_device_owner != nullptr, parameters);
    mfxStatus status =
        MFXVideoENCODE_Query(runtime->state_->session.session(), &parameters, &parameters);
    if (status < MFX_ERR_NONE) {
        error = "oneVPL encoder Query failed with status " + std::to_string(status);
        return nullptr;
    }
    status = MFXVideoENCODE_Init(runtime->state_->session.session(), &parameters);
    if (status < MFX_ERR_NONE) {
        error = "oneVPL encoder Init failed with status " + std::to_string(status);
        return nullptr;
    }
    runtime->state_->session.mark_initialized(VplSession::Component::kEncode);

    mfxVideoParam actual{};
    if (MFXVideoENCODE_GetVideoParam(runtime->state_->session.session(), &actual) != MFX_ERR_NONE) {
        error = "oneVPL failed to report encoder parameters";
        return nullptr;
    }
    if (external_device_owner && actual.IOPattern != MFX_IOPATTERN_IN_VIDEO_MEMORY) {
        error = "oneVPL external encoder did not preserve video-memory input";
        return nullptr;
    }
    const size_t suggested = static_cast<size_t>(actual.mfx.BufferSizeInKB) * 1000u *
                             std::max<mfxU16>(1, actual.mfx.BRCParamMultiplier);
    bitstream_capacity = std::max<size_t>(suggested, 1024u * 1024u);
    if (bitstream_capacity > std::numeric_limits<mfxU32>::max()) {
        error = "oneVPL bitstream buffer exceeds API limits";
        return nullptr;
    }
    return runtime;
}

mfxSession VplEncoderRuntime::session() const noexcept { return state_->session.session(); }

}  // namespace mkvc::gpu::intel
