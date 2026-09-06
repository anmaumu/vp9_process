#include "gpu/intel/vpl_decoder_runtime.hpp"

#include <vpl/mfxdispatcher.h>

#include "gpu/intel/test_render_node_filter.hpp"
#include "gpu/intel/vpl_session.hpp"
#include "mkvcodec/mkvc.h"

namespace mkvc::gpu::intel {
namespace {

mfxU32 vpl_codec(uint32_t codec) { return codec == MKVC_CODEC_VP9 ? MFX_CODEC_VP9 : MFX_CODEC_AV1; }

}  // namespace

std::unique_ptr<VplDecoderRuntime> VplDecoderRuntime::create(uint32_t codec, std::string& error) {
    auto runtime = std::unique_ptr<VplDecoderRuntime>(new VplDecoderRuntime());
    runtime->lifetime_ = std::make_shared<VplSession>();
    if (!runtime->lifetime_->load()) {
        error = "MFXLoad failed";
        return nullptr;
    }

    mfxLoader loader = runtime->lifetime_->loader();
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
        error = "oneVPL hardware decoder filter failed";
        return nullptr;
    }
    value.Data.U32 = vpl_codec(codec);
#if defined(MKVC_ENABLE_TEST_HOOKS) && defined(__linux__)
    if (!test_render_node_filter(loader)) {
        error = "invalid or unsupported test Intel render-node filter";
        return nullptr;
    }
#endif
    if (MFXSetConfigFilterProperty(codec_filter,
                                   reinterpret_cast<const mfxU8*>(
                                       "mfxImplDescription.mfxDecoderDescription.decoder.CodecID"),
                                   value) != MFX_ERR_NONE ||
        runtime->lifetime_->create_session() != MFX_ERR_NONE) {
        error = "no matching Intel hardware decoder is available";
        return nullptr;
    }
    return runtime;
}

mfxSession VplDecoderRuntime::session() const noexcept { return lifetime_->session(); }

}  // namespace mkvc::gpu::intel
