#include "intel_surface_factory.hpp"

#include "intel_completion.hpp"
#include "intel_native_handle.hpp"

#include <cstdint>
#include <optional>

namespace mkvc::gpu::intel {

mkvc_result wrap_vpl_surface(
    mfxSession session, mfxFrameSurface1* surface, mfxSyncPoint sync_point,
    uint64_t device_id, const std::shared_ptr<void>& session_keepalive,
    const std::shared_ptr<GpuFramePool>& pool,
    GpuFramePool::Acquisition& output, std::string& error) {
    output = {};
    if (session == nullptr || surface == nullptr || surface->FrameInterface == nullptr ||
        sync_point == nullptr || !pool) {
        error = "invalid oneVPL GPU surface arguments";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const auto& info = surface->Info;
    if ((info.FourCC != MFX_FOURCC_NV12 && info.FourCC != MFX_FOURCC_P010) ||
        info.CropW == 0 || info.CropH == 0) {
        error = "unsupported oneVPL GPU surface format";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    mfxHDL resource = nullptr;
    mfxResourceType resource_type = static_cast<mfxResourceType>(0);
    const mfxStatus native_status = surface->FrameInterface->GetNativeHandle(
        surface, &resource, &resource_type);
    if (native_status != MFX_ERR_NONE || resource == nullptr) {
        error = "oneVPL failed to export a native video-memory surface";
        return MKVC_ERROR_NOT_SUPPORTED;
    }

    mkvc_gpu_native_handle_desc native{};
#if defined(_WIN32)
    if (resource_type != MFX_RESOURCE_DX11_TEXTURE) {
        error = "oneVPL surface is not a D3D11 texture";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    const auto* pair = static_cast<const mfxHDLPair*>(resource);
    const mkvc_result native_result = make_d3d11_handle(
        device_id, 0, reinterpret_cast<uintptr_t>(pair->first),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pair->second)),
        native, error);
#else
    if (resource_type != MFX_RESOURCE_VA_SURFACE) {
        error = "oneVPL surface is not a VA-API surface";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    mfxHDL display = nullptr;
    if (MFXVideoCORE_GetHandle(session, MFX_HANDLE_VA_DISPLAY, &display) !=
            MFX_ERR_NONE || display == nullptr) {
        error = "oneVPL failed to export its VA display";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    const uint32_t va_surface = *static_cast<const uint32_t*>(resource);
    const mkvc_result native_result = make_va_surface_handle(
        device_id, 0, reinterpret_cast<uintptr_t>(display), va_surface,
        native, error);
#endif
    if (native_result != MKVC_OK) return native_result;

    mkvc_gpu_frame_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.struct_version = 1;
    desc.backend = MKVC_BACKEND_INTEL;
#if defined(_WIN32)
    desc.memory_type = MKVC_GPU_MEMORY_D3D11_TEXTURE;
#else
    desc.memory_type = MKVC_GPU_MEMORY_VA_SURFACE;
#endif
    desc.device_id = device_id;
    desc.pixel_format = info.FourCC == MFX_FOURCC_P010
        ? MKVC_PIXEL_FORMAT_P010 : MKVC_PIXEL_FORMAT_NV12;
    desc.width = info.CropW;
    desc.height = info.CropH;
    desc.plane_count = 2;
    desc.pts = static_cast<int64_t>(surface->Data.TimeStamp);
    auto completion = make_sync_point_completion(
        session, sync_point, session_keepalive);
    return pool->acquire(
        desc, std::move(completion), native,
        [surface, keepalive = session_keepalive] {
            (void)keepalive;
            surface->FrameInterface->Release(surface);
        }, output, error);
}

}  // namespace mkvc::gpu::intel
