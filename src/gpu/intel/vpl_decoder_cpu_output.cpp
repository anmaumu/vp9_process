#include "gpu/intel/vpl_decoder_cpu_output.hpp"

#include <libyuv/convert.h>

#include <cstdint>
#include <limits>
#include <utility>

#include "cpu_vp9_decoder.hpp"

namespace mkvc::gpu::intel {

mkvc_result copy_vpl_surface_to_i420(mfxFrameSurface1* surface,
                                     std::unique_ptr<DecodedFrame>& frame, std::string& error) {
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

}  // namespace mkvc::gpu::intel
