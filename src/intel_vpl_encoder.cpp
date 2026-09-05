#include "intel_vpl_encoder.hpp"

#include "gpu/gpu_frame.hpp"
#include "gpu/intel/test_render_node_filter.hpp"

#if defined(MKVC_HAS_INTEL_ONEVPL)
#include <vpl/mfxdispatcher.h>
#include <vpl/mfxmemory.h>
#include <vpl/mfxvideo.h>

#include "gpu/intel/intel_import_contract.hpp"
#include "gpu/intel/vpl_bitstream.hpp"
#include "gpu/intel/vpl_session.hpp"
#include "gpu/intel/vpl_surface_import.hpp"
#if defined(_WIN32)
#include <d3d11.h>
#include <wrl/client.h>
#endif
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <limits>
#include <thread>
#include <utility>

namespace mkvc {

struct IntelVplEncoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    struct Pending {
        mfxBitstream bitstream{};
        std::vector<uint8_t> storage;
        mfxSyncPoint sync = nullptr;
        std::shared_ptr<gpu::ManualCompletion> input_completion;
        std::weak_ptr<gpu::GpuFrameCore> input_frame;
    };
    struct ImportedInput {
        mfxFrameSurface1* surface = nullptr;
        mkvc_gpu_frame* owner = nullptr;
        ~ImportedInput() {
            // VA handles have no native refcount: destroy the imported wrapper
            // before allowing the application to destroy its original surface.
            if (surface) surface->FrameInterface->Release(surface);
            mkvc_gpu_frame_release(owner);
        }
    };
    mfxLoader loader = nullptr;
    mfxSession session = nullptr;
    std::unique_ptr<gpu::intel::VplSession> lifetime;
    mfxVideoParam parameters{};
    std::deque<std::unique_ptr<Pending>> pending;
    std::vector<std::unique_ptr<ImportedInput>> imported_inputs;
    size_t bitstream_capacity = 0;
#endif
    uint32_t codec = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    int64_t next_pts = 0;
    uint32_t async_depth = 4;
    std::atomic<uint32_t> max_pending{0};
#if defined(MKVC_ENABLE_TEST_HOOKS)
    uint32_t test_device_loss_after = std::numeric_limits<uint32_t>::max();
    uint32_t collected_syncpoints = 0;
#endif
    bool encoder_initialized = false;
    // A real lease (not merely shared_ptr ownership) prevents recycling the
    // external resource whose owner also guarantees the borrowed device lifetime.
    mkvc_gpu_frame* external_device_owner = nullptr;
    uint64_t external_device_identity = 0;
    bool drained = false;
    bool closed = false;
};

IntelVplEncoder::IntelVplEncoder() : impl_(std::make_unique<Impl>()) {}
IntelVplEncoder::~IntelVplEncoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_INTEL_ONEVPL)
namespace {

constexpr mfxU32 kSyncWaitMs = 100;
constexpr size_t kMaxImportedInputs = 64;

void retire_imported_inputs(IntelVplEncoder::Impl& impl) {
    auto& inputs = impl.imported_inputs;
    for (auto it = inputs.begin(); it != inputs.end();) {
        auto* surface = (*it)->surface;
        // Output SyncPoint alone does not prove an imported surface is unused.
        // Keep our reference while checking the runtime's remaining ownership.
        if (gpu::intel::can_release_imported_surface(surface)) {
            it = inputs.erase(it);
        } else {
            ++it;
        }
    }
}

mfxU32 vpl_codec(uint32_t codec) { return codec == MKVC_CODEC_VP9 ? MFX_CODEC_VP9 : MFX_CODEC_AV1; }

mkvc_result collect_output(IntelVplEncoder::Impl& impl, IntelVplEncoder::Impl::Pending& pending,
                           std::vector<IntelEncodedPacket>& packets, std::string& error) {
    if (pending.sync == nullptr) return MKVC_OK;
    mfxStatus status;
    do {
        status = MFXVideoCORE_SyncOperation(impl.session, pending.sync, kSyncWaitMs);
    } while (status == MFX_WRN_IN_EXECUTION);
    if (status != MFX_ERR_NONE) {
        error = "oneVPL SyncOperation failed with status " + std::to_string(status);
        if (pending.input_completion) pending.input_completion->fail(error);
        return status == MFX_ERR_DEVICE_LOST ? MKVC_ERROR_IO : MKVC_ERROR_CODEC;
    }
    const mkvc_result packet_result = gpu::intel::append_vpl_encoded_packet(
        pending.bitstream, impl.codec, impl.fps_num, impl.fps_den, packets, error);
    if (packet_result != MKVC_OK) {
        if (pending.input_completion) pending.input_completion->fail(error);
        return packet_result;
    }
    if (pending.input_completion) pending.input_completion->complete();
    if (auto frame = pending.input_frame.lock()) frame->poll_recycle();
    return MKVC_OK;
}

mkvc_result collect_oldest(IntelVplEncoder::Impl& impl, std::vector<IntelEncodedPacket>& packets,
                           std::string& error) {
    if (impl.pending.empty()) return MKVC_OK;
    auto pending = std::move(impl.pending.front());
    impl.pending.pop_front();
#if defined(MKVC_ENABLE_TEST_HOOKS)
    if (impl.collected_syncpoints >= impl.test_device_loss_after) {
        mfxStatus status;
        do {
            status = MFXVideoCORE_SyncOperation(impl.session, pending->sync, kSyncWaitMs);
        } while (status == MFX_WRN_IN_EXECUTION);
        error = "injected Intel encoder device loss";
        if (pending->input_completion) pending->input_completion->fail(error);
        return MKVC_ERROR_IO;
    }
#endif
    const mkvc_result result = collect_output(impl, *pending, packets, error);
#if defined(MKVC_ENABLE_TEST_HOOKS)
    if (result == MKVC_OK) ++impl.collected_syncpoints;
#endif
    return result;
}

mkvc_result submit_surface(IntelVplEncoder::Impl& impl, mfxFrameSurface1* surface,
                           std::string& error,
                           std::shared_ptr<gpu::ManualCompletion> completion = {},
                           std::weak_ptr<gpu::GpuFrameCore> input_frame = {}) {
    auto pending = std::make_unique<IntelVplEncoder::Impl::Pending>();
    pending->storage.resize(impl.bitstream_capacity);
    pending->bitstream.Data = pending->storage.data();
    pending->bitstream.MaxLength = static_cast<mfxU32>(pending->storage.size());
    pending->input_completion = std::move(completion);
    pending->input_frame = std::move(input_frame);
    mfxStatus status;
    do {
        status = MFXVideoENCODE_EncodeFrameAsync(impl.session, nullptr, surface,
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
        impl.pending.push_back(std::move(pending));
        const uint32_t observed = static_cast<uint32_t>(impl.pending.size());
        uint32_t current = impl.max_pending.load(std::memory_order_relaxed);
        while (current < observed && !impl.max_pending.compare_exchange_weak(
                                         current, observed, std::memory_order_relaxed)) {
        }
    } else if (pending->input_completion) {
        // The implementation consumed the input synchronously or buffered it
        // without returning a SyncPoint, so it no longer owns the surface.
        pending->input_completion->complete();
        if (auto frame = pending->input_frame.lock()) frame->poll_recycle();
    }
    return MKVC_OK;
}

}  // namespace
#endif

std::unique_ptr<IntelVplEncoder> IntelVplEncoder::create(
    uint32_t codec, uint32_t width, uint32_t height, uint32_t fps_num, uint32_t fps_den,
    uint32_t quality, uint32_t keyframe_interval_frames, std::string& error, uint32_t async_depth,
    const std::shared_ptr<gpu::GpuFrameCore>& external_device_owner) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)codec;
    (void)width;
    (void)height;
    (void)fps_num;
    (void)fps_den;
    (void)quality;
    (void)keyframe_interval_frames;
    (void)async_depth;
    (void)external_device_owner;
    error = "Intel oneVPL backend was not built";
    return nullptr;
#else
    if ((codec != MKVC_CODEC_VP9 && codec != MKVC_CODEC_AV1) || width == 0 || height == 0 ||
        width > 65520u || height > 65520u || (width & 1u) != 0 || (height & 1u) != 0 ||
        fps_num == 0 || fps_den == 0 || quality > 63 || async_depth == 0 || async_depth > 8) {
        error = "invalid oneVPL encoder configuration";
        return nullptr;
    }
    auto encoder = std::unique_ptr<IntelVplEncoder>(new IntelVplEncoder());
    auto& impl = *encoder->impl_;
    impl.codec = codec;
    impl.width = width;
    impl.height = height;
    impl.fps_num = fps_num;
    impl.fps_den = fps_den;
    impl.async_depth = async_depth;
    impl.lifetime = std::make_unique<gpu::intel::VplSession>();
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
        error = "oneVPL hardware filter failed";
        return nullptr;
    }
    value.Data.U32 = vpl_codec(codec);
    if (MFXSetConfigFilterProperty(codec_filter,
                                   reinterpret_cast<const mfxU8*>(
                                       "mfxImplDescription.mfxEncoderDescription.encoder.CodecID"),
                                   value) != MFX_ERR_NONE) {
        error = "oneVPL encoder codec filter failed";
        return nullptr;
    }
    if (external_device_owner) {
        mkvc_gpu_native_handle_desc native{};
        if (external_device_owner->get_native_handle(native, error) != MKVC_OK) return nullptr;
        mfxHandleType handle_type;
        mfxHDL device = nullptr;
#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D11Device> texture_device;
        if (native.type != MKVC_GPU_NATIVE_D3D11_TEXTURE || native.handles[0] == 0) {
            error = "external Intel encoder requires a D3D11 texture";
            return nullptr;
        }
        reinterpret_cast<ID3D11Texture2D*>(static_cast<uintptr_t>(native.handles[0]))
            ->GetDevice(texture_device.GetAddressOf());
        device = texture_device.Get();
        handle_type = MFX_HANDLE_D3D11_DEVICE;
#else
        if (native.type != MKVC_GPU_NATIVE_VA_SURFACE || native.handles[0] == 0) {
            error = "external Intel encoder requires a VA display/surface";
            return nullptr;
        }
        device = reinterpret_cast<mfxHDL>(static_cast<uintptr_t>(native.handles[0]));
        handle_type = MFX_HANDLE_VA_DISPLAY;
#endif
        impl.external_device_owner = gpu::make_handle(external_device_owner);
        impl.external_device_identity = reinterpret_cast<uintptr_t>(device);
        mfxConfig type_config = MFXCreateConfig(impl.loader);
        mfxConfig device_config = MFXCreateConfig(impl.loader);
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
            return nullptr;
        }
    }
#if defined(MKVC_ENABLE_TEST_HOOKS) && defined(__linux__)
    if (!gpu::intel::test_render_node_filter(impl.loader)) {
        error = "invalid or unsupported test Intel render-node filter";
        return nullptr;
    }
#endif
    if (impl.lifetime->create_session() != MFX_ERR_NONE) {
        error = "no matching Intel hardware encoder is available";
        return nullptr;
    }
    impl.session = impl.lifetime->session();
    auto& parameters = impl.parameters;
    parameters.mfx.CodecId = vpl_codec(codec);
    parameters.mfx.CodecProfile =
        static_cast<mfxU16>(codec == MKVC_CODEC_VP9 ? MFX_PROFILE_VP9_0 : MFX_PROFILE_AV1_MAIN);
    if (codec == MKVC_CODEC_AV1) parameters.mfx.CodecLevel = MFX_LEVEL_AV1_63;
    parameters.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    parameters.mfx.RateControlMethod = MFX_RATECONTROL_CQP;
    parameters.mfx.QPI = static_cast<mfxU16>(quality);
    parameters.mfx.QPP = static_cast<mfxU16>(quality);
    parameters.mfx.QPB = static_cast<mfxU16>(quality);
    const uint64_t default_gop =
        std::max<uint64_t>(1, static_cast<uint64_t>(fps_num) * 4 / fps_den);
    parameters.mfx.GopPicSize = static_cast<mfxU16>(
        std::min<uint64_t>(keyframe_interval_frames == 0 ? default_gop : keyframe_interval_frames,
                           std::numeric_limits<mfxU16>::max()));
    parameters.mfx.GopRefDist = 1;
    parameters.mfx.FrameInfo.FrameRateExtN = fps_num;
    parameters.mfx.FrameInfo.FrameRateExtD = fps_den;
    parameters.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    parameters.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    parameters.mfx.FrameInfo.CropW = static_cast<mfxU16>(width);
    parameters.mfx.FrameInfo.CropH = static_cast<mfxU16>(height);
    parameters.mfx.FrameInfo.Width = static_cast<mfxU16>((width + 15u) & ~15u);
    parameters.mfx.FrameInfo.Height = static_cast<mfxU16>((height + 15u) & ~15u);
    parameters.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    parameters.IOPattern = static_cast<mfxU16>(
        external_device_owner ? MFX_IOPATTERN_IN_VIDEO_MEMORY : MFX_IOPATTERN_IN_SYSTEM_MEMORY);
    parameters.AsyncDepth = static_cast<mfxU16>(async_depth);
    mfxStatus status = MFXVideoENCODE_Query(impl.session, &parameters, &parameters);
    if (status < MFX_ERR_NONE) {
        error = "oneVPL encoder Query failed with status " + std::to_string(status);
        return nullptr;
    }
    status = MFXVideoENCODE_Init(impl.session, &parameters);
    if (status < MFX_ERR_NONE) {
        error = "oneVPL encoder Init failed with status " + std::to_string(status);
        return nullptr;
    }
    impl.encoder_initialized = true;
    impl.lifetime->mark_initialized(gpu::intel::VplSession::Component::kEncode);
    mfxVideoParam actual{};
    if (MFXVideoENCODE_GetVideoParam(impl.session, &actual) != MFX_ERR_NONE) {
        error = "oneVPL failed to report encoder parameters";
        return nullptr;
    }
    if (external_device_owner && actual.IOPattern != MFX_IOPATTERN_IN_VIDEO_MEMORY) {
        error = "oneVPL external encoder did not preserve video-memory input";
        return nullptr;
    }
    const size_t suggested = static_cast<size_t>(actual.mfx.BufferSizeInKB) * 1000u *
                             std::max<mfxU16>(1, actual.mfx.BRCParamMultiplier);
    impl.bitstream_capacity = std::max<size_t>(suggested, 1024u * 1024u);
    if (impl.bitstream_capacity > std::numeric_limits<mfxU32>::max()) {
        error = "oneVPL bitstream buffer exceeds API limits";
        return nullptr;
    }
    return encoder;
#endif
}

mkvc_result IntelVplEncoder::write_gpu_surface(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                                               int64_t pts,
                                               std::vector<IntelEncodedPacket>& packets,
                                               std::string& error) {
    packets.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)frame;
    (void)pts;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained) {
        error = "oneVPL encoder is closed or drained";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (!frame || frame->desc().backend != MKVC_BACKEND_INTEL ||
        frame->desc().pixel_format != MKVC_PIXEL_FORMAT_NV12 || frame->desc().width != impl.width ||
        frame->desc().height != impl.height) {
        error = "GPU frame is not a compatible Intel NV12 surface";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    retire_imported_inputs(impl);
    if (impl.external_device_owner != nullptr) {
        mkvc_gpu_native_handle_desc native{};
        if (frame->get_native_handle(native, error) != MKVC_OK) return MKVC_ERROR_INVALID_ARGUMENT;
        uint64_t identity = 0;
#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        if (native.type == MKVC_GPU_NATIVE_D3D11_TEXTURE && native.handles[0] != 0) {
            reinterpret_cast<ID3D11Texture2D*>(static_cast<uintptr_t>(native.handles[0]))
                ->GetDevice(device.GetAddressOf());
            identity = reinterpret_cast<uintptr_t>(device.Get());
        }
#else
        if (native.type == MKVC_GPU_NATIVE_VA_SURFACE) identity = native.handles[0];
#endif
        if (identity != impl.external_device_identity) {
            error = "external Intel frame belongs to a different device/display; flush first";
            return MKVC_ERROR_INVALID_ARGUMENT;
        }
    }
    // Separate decode/encode sessions cannot share a SyncPoint dependency. Waiting
    // here keeps pixels resident while establishing readiness deterministically.
    mkvc_result result =
        frame->producer_completion()->wait(std::numeric_limits<uint32_t>::max(), error);
    if (result != MKVC_OK) return result;
    const auto resource = frame->backend_resource();
    mfxFrameSurface1* surface = nullptr;
    bool imported = false;
    if (resource.kind == gpu::BackendResourceKind::kIntelVplSurface && resource.object != nullptr) {
        surface = static_cast<mfxFrameSurface1*>(resource.object);
    } else {
        if (impl.imported_inputs.size() >= kMaxImportedInputs) {
            error = "Intel imported surface retention limit reached; flush before retrying";
            return MKVC_WOULD_BLOCK;
        }
        auto input = std::make_unique<Impl::ImportedInput>();
        result = gpu::intel::import_vpl_encode_surface(impl.session, frame, surface, error);
        if (result != MKVC_OK) return result;
        input->surface = surface;
        input->owner = gpu::make_handle(frame);
        impl.imported_inputs.push_back(std::move(input));
        imported = true;
    }
    const int64_t frame_pts = pts >= 0 ? pts : impl.next_pts;
    const mfxU64 original_timestamp = surface->Data.TimeStamp;
    surface->Data.TimeStamp =
        static_cast<mfxU64>(frame_pts) * 90000ULL * impl.fps_den / impl.fps_num;
    auto completion = std::make_shared<gpu::ManualCompletion>();
    result = submit_surface(impl, surface, error, completion, frame);
    // Imported wrappers are private to this encoder. AV1 may read their
    // metadata asynchronously after EncodeFrameAsync returns: restoring the
    // initial (usually zero) timestamp here corrupts every output PTS.
    // Keep submitted metadata until the imported wrapper is retired.
    if (!imported) surface->Data.TimeStamp = original_timestamp;
    if (result != MKVC_OK && result != MKVC_END_OF_STREAM) {
        completion->fail(error);
        return result;
    }
    if (result == MKVC_OK) {
        result = frame->add_consumer(completion, error);
        if (result != MKVC_OK) return result;
    } else {
        completion->complete();
    }
    // A decoder-owned pool can be smaller than the encoder reorder window.
    // Complete one submitted dependency per call so the upstream pool always
    // makes progress. This remains GPU-resident; only the host API waits.
    if (result == MKVC_OK && !impl.pending.empty()) {
        result = collect_oldest(impl, packets, error);
    }
    if (imported) retire_imported_inputs(impl);
    if (result == MKVC_OK || result == MKVC_END_OF_STREAM) {
        impl.next_pts = std::max(impl.next_pts, frame_pts + 1);
        return MKVC_OK;
    }
    return result;
#endif
}

mkvc_result IntelVplEncoder::write_nv12(const uint8_t* y, int32_t y_stride, const uint8_t* uv,
                                        int32_t uv_stride, int64_t pts,
                                        std::vector<IntelEncodedPacket>& packets,
                                        std::string& error) {
    packets.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)y;
    (void)y_stride;
    (void)uv;
    (void)uv_stride;
    (void)pts;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained) {
        error = "oneVPL encoder is closed or drained";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (y == nullptr || uv == nullptr || y_stride < static_cast<int32_t>(impl.width) ||
        uv_stride < static_cast<int32_t>(impl.width)) {
        error = "invalid NV12 input";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    mfxFrameSurface1* surface = nullptr;
    if (MFXMemory_GetSurfaceForEncode(impl.session, &surface) != MFX_ERR_NONE ||
        surface == nullptr) {
        error = "oneVPL failed to acquire an encode surface";
        return MKVC_ERROR_CODEC;
    }
    mfxStatus status = surface->FrameInterface->Map(surface, MFX_MAP_WRITE);
    if (status != MFX_ERR_NONE) {
        surface->FrameInterface->Release(surface);
        error = "oneVPL failed to map an encode surface";
        return MKVC_ERROR_CODEC;
    }
    const uint32_t pitch =
        (static_cast<uint32_t>(surface->Data.PitchHigh) << 16) | surface->Data.PitchLow;
    for (uint32_t row = 0; row < impl.height; ++row) {
        std::memcpy(surface->Data.Y + static_cast<size_t>(row) * pitch,
                    y + static_cast<size_t>(row) * y_stride, impl.width);
    }
    for (uint32_t row = 0; row < impl.height / 2; ++row) {
        std::memcpy(surface->Data.UV + static_cast<size_t>(row) * pitch,
                    uv + static_cast<size_t>(row) * uv_stride, impl.width);
    }
    const int64_t frame_pts = pts >= 0 ? pts : impl.next_pts;
    surface->Data.TimeStamp =
        static_cast<mfxU64>(frame_pts) * 90000ULL * impl.fps_den / impl.fps_num;
    status = surface->FrameInterface->Unmap(surface);
    if (status != MFX_ERR_NONE) {
        surface->FrameInterface->Release(surface);
        error = "oneVPL failed to unmap an encode surface";
        return MKVC_ERROR_CODEC;
    }
    mkvc_result result = submit_surface(impl, surface, error);
    surface->FrameInterface->Release(surface);
    if (result == MKVC_OK && impl.pending.size() >= impl.async_depth) {
        result = collect_oldest(impl, packets, error);
    }
    if (result == MKVC_OK || result == MKVC_END_OF_STREAM) {
        impl.next_pts = std::max(impl.next_pts, frame_pts + 1);
        return MKVC_OK;
    }
    return result;
#endif
}

mkvc_result IntelVplEncoder::drain(std::vector<IntelEncodedPacket>& packets, std::string& error) {
    packets.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained) return MKVC_OK;
    while (true) {
        const mkvc_result result = submit_surface(impl, nullptr, error);
        if (result == MKVC_END_OF_STREAM) break;
        if (result != MKVC_OK) return result;
        if (impl.pending.size() >= impl.async_depth) {
            const mkvc_result collected = collect_oldest(impl, packets, error);
            if (collected != MKVC_OK) return collected;
        }
    }
    while (!impl.pending.empty()) {
        const mkvc_result result = collect_oldest(impl, packets, error);
        if (result != MKVC_OK) return result;
    }
    impl.drained = true;
    return MKVC_OK;
#endif
}

mkvc_result IntelVplEncoder::close(std::string& error) {
    (void)error;
    if (impl_->closed) return MKVC_OK;
#if defined(MKVC_HAS_INTEL_ONEVPL)
    for (const auto& pending : impl_->pending) {
        if (pending->sync != nullptr) {
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
    }
    impl_->pending.clear();
    // Release our wrapper references while the component still exists. Keep
    // original VA resources alive until runtime teardown drops its references.
    for (auto& input : impl_->imported_inputs) {
        input->surface->FrameInterface->Release(input->surface);
        input->surface = nullptr;
    }
    impl_->lifetime.reset();
    impl_->encoder_initialized = false;
    impl_->session = nullptr;
    impl_->loader = nullptr;
    impl_->imported_inputs.clear();
#endif
    mkvc_gpu_frame_release(impl_->external_device_owner);
    impl_->external_device_owner = nullptr;
    impl_->closed = true;
    return MKVC_OK;
}

uint32_t IntelVplEncoder::max_pending_observed() const {
    return impl_->max_pending.load(std::memory_order_relaxed);
}

#if defined(MKVC_ENABLE_TEST_HOOKS)
void IntelVplEncoder::set_test_device_loss_after(uint32_t completed_syncpoints) {
    impl_->test_device_loss_after = completed_syncpoints;
}
#endif

}  // namespace mkvc
