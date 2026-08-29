#include "intel_vpl_probe.hpp"

#if defined(MKVC_HAS_INTEL_ONEVPL)
#include <vpl/mfxdispatcher.h>
#include <vpl/mfxvideo.h>
#endif

namespace mkvc {

#if defined(MKVC_HAS_INTEL_ONEVPL)
namespace {

void set_frame_info(mfxFrameInfo& info) {
    info.FourCC = MFX_FOURCC_NV12;
    info.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    info.Width = 1920;
    info.Height = 1088;
    info.CropW = 1920;
    info.CropH = 1080;
    info.FrameRateExtN = 30;
    info.FrameRateExtD = 1;
    info.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
}

bool query_decode(mfxSession session, mfxU32 codec) {
    // Mode 1 queries codec-class configurability without assuming a stream
    // profile; exact frame parameters are validated after header parsing.
    mfxVideoParam output{};
    output.mfx.CodecId = codec;
    const mfxStatus status = MFXVideoDECODE_Query(session, nullptr, &output);
    return status >= MFX_ERR_NONE && output.mfx.CodecId == codec;
}

bool query_encode(mfxSession session, mfxU32 codec) {
    mfxVideoParam input{};
    input.mfx.CodecId = codec;
    input.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    input.mfx.RateControlMethod = MFX_RATECONTROL_CQP;
    input.mfx.QPI = 32;
    input.mfx.QPP = 32;
    input.mfx.QPB = 32;
    input.mfx.GopPicSize = 120;
    input.mfx.GopRefDist = 1;
    input.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;
    input.AsyncDepth = 4;
    set_frame_info(input.mfx.FrameInfo);
    mfxVideoParam output = input;
    const mfxStatus status = MFXVideoENCODE_Query(session, &input, &output);
    return status >= MFX_ERR_NONE && output.mfx.CodecId == codec;
}

}  // namespace
#endif

IntelVplProbeResult probe_intel_vpl() {
    IntelVplProbeResult result;
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    result.unavailable_reason = "oneVPL support was not built";
    return result;
#else
    mfxLoader loader = MFXLoad();
    if (loader == nullptr) {
        result.unavailable_reason = "MFXLoad failed";
        return result;
    }
    mfxConfig config = MFXCreateConfig(loader);
    if (config == nullptr) {
        result.unavailable_reason = "MFXCreateConfig failed";
        MFXUnload(loader);
        return result;
    }
    mfxVariant hardware{};
    hardware.Type = MFX_VARIANT_TYPE_U32;
    hardware.Data.U32 = MFX_IMPL_TYPE_HARDWARE;
    const auto* property = reinterpret_cast<const mfxU8*>(
        "mfxImplDescription.Impl");
    if (MFXSetConfigFilterProperty(config, property, hardware) != MFX_ERR_NONE) {
        result.unavailable_reason = "oneVPL hardware filter was rejected";
        MFXUnload(loader);
        return result;
    }
    mfxSession session = nullptr;
    const mfxStatus create_status = MFXCreateSession(loader, 0, &session);
    if (create_status != MFX_ERR_NONE || session == nullptr) {
        result.unavailable_reason = "no oneVPL hardware implementation is available";
        MFXUnload(loader);
        return result;
    }
    result.runtime_available = true;
    mfxVersion version{};
    if (MFXQueryVersion(session, &version) == MFX_ERR_NONE) {
        result.api_major = version.Major;
        result.api_minor = version.Minor;
    }
    result.vp9_decode = query_decode(session, MFX_CODEC_VP9);
    result.vp9_encode = query_encode(session, MFX_CODEC_VP9);
    result.av1_decode = query_decode(session, MFX_CODEC_AV1);
    result.av1_encode = query_encode(session, MFX_CODEC_AV1);
    MFXClose(session);
    MFXUnload(loader);
    return result;
#endif
}

}  // namespace mkvc
