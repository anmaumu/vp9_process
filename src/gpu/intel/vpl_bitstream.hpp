#pragma once

#include <vpl/mfxvideo.h>

#include <cstdint>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc {
struct IntelEncodedPacket;
}

namespace mkvc::gpu::intel {

/**
 * @brief Convert one completed oneVPL bitstream into a container-ready packet.
 *
 * @param bitstream Completed oneVPL output buffer.
 * @param codec MKVC_CODEC_VP9 or MKVC_CODEC_AV1.
 * @param fps_num Stream frame-rate numerator.
 * @param fps_den Stream frame-rate denominator.
 * @param packets Destination packet list; unchanged for empty output or failure.
 * @param error Receives a validation diagnostic.
 * @return MKVC_OK, or MKVC_ERROR_CODEC for malformed VP9 IVF framing.
 *
 * oneVPL may prefix VP9 output with an IVF file header and always supplies an
 * IVF per-frame header. Those container bytes are removed before libwebm muxing.
 */
mkvc_result append_vpl_encoded_packet(const mfxBitstream& bitstream, uint32_t codec,
                                      uint32_t fps_num, uint32_t fps_den,
                                      std::vector<IntelEncodedPacket>& packets, std::string& error);

}  // namespace mkvc::gpu::intel
