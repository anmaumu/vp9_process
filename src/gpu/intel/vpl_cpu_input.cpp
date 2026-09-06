#include "gpu/intel/vpl_cpu_input.hpp"

#include <vpl/mfxmemory.h>

#include <algorithm>
#include <cstring>

#include "gpu/intel/vpl_encoder_queue.hpp"

namespace mkvc::gpu::intel {

mkvc_result submit_cpu_nv12(mfxSession session, VplEncoderQueue& queue, uint32_t width,
                            uint32_t height, uint32_t fps_num, uint32_t fps_den, int64_t& next_pts,
                            const uint8_t* y, int32_t y_stride, const uint8_t* uv,
                            int32_t uv_stride, int64_t pts,
                            std::vector<IntelEncodedPacket>& packets, std::string& error) {
    if (y == nullptr || uv == nullptr || y_stride < static_cast<int32_t>(width) ||
        uv_stride < static_cast<int32_t>(width)) {
        error = "invalid NV12 input";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }

    mfxFrameSurface1* surface = nullptr;
    if (MFXMemory_GetSurfaceForEncode(session, &surface) != MFX_ERR_NONE || surface == nullptr) {
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
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(surface->Data.Y + static_cast<size_t>(row) * pitch,
                    y + static_cast<size_t>(row) * y_stride, width);
    }
    for (uint32_t row = 0; row < height / 2; ++row) {
        std::memcpy(surface->Data.UV + static_cast<size_t>(row) * pitch,
                    uv + static_cast<size_t>(row) * uv_stride, width);
    }
    const int64_t frame_pts = pts >= 0 ? pts : next_pts;
    surface->Data.TimeStamp = static_cast<mfxU64>(frame_pts) * 90000ULL * fps_den / fps_num;
    status = surface->FrameInterface->Unmap(surface);
    if (status != MFX_ERR_NONE) {
        surface->FrameInterface->Release(surface);
        error = "oneVPL failed to unmap an encode surface";
        return MKVC_ERROR_CODEC;
    }

    mkvc_result result = queue.submit(surface, error);
    surface->FrameInterface->Release(surface);
    if (result == MKVC_OK && queue.pending_count() >= queue.async_depth()) {
        result = queue.collect_oldest(packets, error);
    }
    if (result == MKVC_OK || result == MKVC_END_OF_STREAM) {
        next_pts = std::max(next_pts, frame_pts + 1);
        return MKVC_OK;
    }
    return result;
}

}  // namespace mkvc::gpu::intel
