#include "vpl_surface_import.hpp"

#include <vpl/mfxmemory.h>

#include <cstdint>
#include <limits>

#include "../gpu_frame.hpp"
#include "intel_import_contract.hpp"

namespace mkvc::gpu::intel {

mkvc_result import_vpl_encode_surface(mfxSession session,
                                      const std::shared_ptr<GpuFrameCore>& frame,
                                      mfxFrameSurface1*& surface, std::string& error) {
    surface = nullptr;
    mkvc_gpu_native_handle_desc native{};
    mkvc_result result = frame->get_native_handle(native, error);
    if (result != MKVC_OK) return result;
    mfxMemoryInterface* memory = nullptr;
    const mfxStatus memory_status = MFXGetMemoryInterface(session, &memory);
    if (memory_status != MFX_ERR_NONE || !has_surface_import(memory)) {
        error = "oneVPL runtime does not expose external surface import (status=" +
                std::to_string(memory_status) + ", interface=" +
                (memory == nullptr ? std::string("null")
                                   : std::to_string(memory->Version.Major) + "." +
                                         std::to_string(memory->Version.Minor)) +
                ")";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    mfxSurfaceHeader* header = nullptr;
#if defined(_WIN32)
    mfxSurfaceD3D11Tex2D external{};
    if (native.type != MKVC_GPU_NATIVE_D3D11_TEXTURE || native.handles[0] == 0 ||
        native.handles[1] != 0) {
        error = "oneVPL D3D11 import requires texture subresource zero";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    external.SurfaceInterface.Header.SurfaceType = MFX_SURFACE_TYPE_D3D11_TEX2D;
    external.SurfaceInterface.Header.SurfaceFlags = MFX_SURFACE_FLAG_IMPORT_SHARED;
    external.SurfaceInterface.Header.StructSize = sizeof(external);
    external.texture2D = reinterpret_cast<mfxHDL>(static_cast<uintptr_t>(native.handles[0]));
    header = &external.SurfaceInterface.Header;
#else
    mfxSurfaceVAAPI external{};
    if (native.type != MKVC_GPU_NATIVE_VA_SURFACE || native.handles[0] == 0 ||
        native.handles[1] > std::numeric_limits<mfxU32>::max()) {
        error = "oneVPL VA import requires a valid display and surface";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    external.SurfaceInterface.Header.SurfaceType = MFX_SURFACE_TYPE_VAAPI;
    external.SurfaceInterface.Header.SurfaceFlags = MFX_SURFACE_FLAG_IMPORT_SHARED;
    external.SurfaceInterface.Header.StructSize = sizeof(external);
    external.vaDisplay = reinterpret_cast<mfxHDL>(static_cast<uintptr_t>(native.handles[0]));
    external.vaSurfaceID = static_cast<mfxU32>(native.handles[1]);
    header = &external.SurfaceInterface.Header;
#endif
    const mfxStatus status =
        memory->ImportFrameSurface(memory, MFX_SURFACE_COMPONENT_ENCODE, header, &surface);
    if (status != MFX_ERR_NONE || surface == nullptr || surface->FrameInterface == nullptr) {
        if (surface != nullptr && surface->FrameInterface != nullptr)
            surface->FrameInterface->Release(surface);
        surface = nullptr;
        error =
            "oneVPL shared external surface import failed with status " + std::to_string(status);
        return status == MFX_ERR_UNSUPPORTED ? MKVC_ERROR_NOT_SUPPORTED : MKVC_ERROR_CODEC;
    }
    if ((header->SurfaceFlags & MFX_SURFACE_FLAG_IMPORT_SHARED) == 0 ||
        (header->SurfaceFlags & MFX_SURFACE_FLAG_IMPORT_COPY) != 0) {
        surface->FrameInterface->Release(surface);
        surface = nullptr;
        error = "oneVPL external surface import would copy pixels";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    return MKVC_OK;
}

}  // namespace mkvc::gpu::intel
