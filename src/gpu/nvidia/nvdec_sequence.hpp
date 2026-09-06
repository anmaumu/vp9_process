#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/dynlink_nvcuvid.h>

#include <cstdint>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc::gpu::nvidia {

class NvdecApi;

/**
 * @brief Validate a parser sequence and create or reuse its NVDEC decoder.
 *
 * @param api Loaded NVCUVID function table.
 * @param format Sequence format supplied by the parser.
 * @param decoder Existing decoder, or null before the first sequence.
 * @param width Current visible width; updated after successful creation.
 * @param height Current visible height; updated after successful creation.
 * @param decode_surfaces Receives the positive parser decode-surface count.
 * @param error Receives a validation, capability or creation diagnostic.
 * @return MKVC_OK when the decoder was created or safely reused.
 *
 * Only uncropped, even-sized, 8-bit 4:2:0 output is accepted. An existing
 * decoder may be reused only when the visible resolution is unchanged.
 */
mkvc_result configure_nvdec_sequence(NvdecApi& api, const CUVIDEOFORMAT& format,
                                     CUvideodecoder& decoder, uint32_t& width, uint32_t& height,
                                     int& decode_surfaces, std::string& error);

}  // namespace mkvc::gpu::nvidia
