#include "nvdec_sequence.hpp"

#include <algorithm>
#include <cstdint>

#include "nvdec_api.hpp"

namespace mkvc::gpu::nvidia {

mkvc_result configure_nvdec_sequence(NvdecApi& api, const CUVIDEOFORMAT& format,
                                     CUvideodecoder& decoder, uint32_t& width, uint32_t& height,
                                     int& decode_surfaces, std::string& error) {
    decode_surfaces = 0;
    if (format.chroma_format != cudaVideoChromaFormat_420 || format.bit_depth_luma_minus8 != 0 ||
        format.bit_depth_chroma_minus8 != 0 || format.display_area.left != 0 ||
        format.display_area.top != 0) {
        error = "NVDEC supports only uncropped 8-bit 4:2:0 input";
        return MKVC_ERROR_CODEC;
    }
    const int visible_width = format.display_area.right;
    const int visible_height = format.display_area.bottom;
    if (visible_width <= 0 || visible_height <= 0 || (visible_width & 1) != 0 ||
        (visible_height & 1) != 0) {
        error = "NVDEC returned invalid display dimensions";
        return MKVC_ERROR_CODEC;
    }

    CUVIDDECODECAPS caps{};
    caps.eCodecType = format.codec;
    caps.eChromaFormat = format.chroma_format;
    caps.nBitDepthMinus8 = format.bit_depth_luma_minus8;
    const uint64_t macroblocks =
        (static_cast<uint64_t>(format.coded_width) * format.coded_height + 255) / 256;
    if (api.decoder_caps(&caps) != CUDA_SUCCESS || caps.bIsSupported == 0 ||
        format.coded_width < caps.nMinWidth || format.coded_width > caps.nMaxWidth ||
        format.coded_height < caps.nMinHeight || format.coded_height > caps.nMaxHeight ||
        macroblocks > caps.nMaxMBCount) {
        error = "NVDEC codec, bit depth, or dimensions are unsupported";
        return MKVC_ERROR_CODEC;
    }

    decode_surfaces = std::max(1, static_cast<int>(format.min_num_decode_surfaces));
    if (decoder != nullptr) {
        if (width == static_cast<uint32_t>(visible_width) &&
            height == static_cast<uint32_t>(visible_height)) {
            return MKVC_OK;
        }
        decode_surfaces = 0;
        error = "NVDEC mid-stream resolution change is not supported";
        return MKVC_ERROR_CODEC;
    }

    CUVIDDECODECREATEINFO info{};
    info.ulWidth = format.coded_width;
    info.ulHeight = format.coded_height;
    info.ulNumDecodeSurfaces = format.min_num_decode_surfaces;
    info.CodecType = format.codec;
    info.ChromaFormat = format.chroma_format;
    info.ulCreationFlags = cudaVideoCreate_PreferCUVID;
    info.bitDepthMinus8 = format.bit_depth_luma_minus8;
    info.ulMaxWidth = format.coded_width;
    info.ulMaxHeight = format.coded_height;
    info.display_area.left = static_cast<short>(format.display_area.left);
    info.display_area.top = static_cast<short>(format.display_area.top);
    info.display_area.right = static_cast<short>(format.display_area.right);
    info.display_area.bottom = static_cast<short>(format.display_area.bottom);
    info.OutputFormat = cudaVideoSurfaceFormat_NV12;
    info.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
    info.ulTargetWidth = static_cast<tcu_ulong>(visible_width);
    info.ulTargetHeight = static_cast<tcu_ulong>(visible_height);
    info.ulNumOutputSurfaces = 8;
    if (api.decoder_create(&decoder, &info) != CUDA_SUCCESS) {
        decode_surfaces = 0;
        error = "cuvidCreateDecoder failed";
        return MKVC_ERROR_CODEC;
    }
    width = static_cast<uint32_t>(visible_width);
    height = static_cast<uint32_t>(visible_height);
    return MKVC_OK;
}

}  // namespace mkvc::gpu::nvidia
