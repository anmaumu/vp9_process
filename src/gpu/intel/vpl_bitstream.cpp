#include "vpl_bitstream.hpp"

#include <cstring>
#include <utility>

#include "../../intel_vpl_encoder.hpp"

namespace mkvc::gpu::intel {
namespace {

uint32_t read_le32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8) |
           (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
}

}  // namespace

mkvc_result append_vpl_encoded_packet(const mfxBitstream& bitstream, uint32_t codec,
                                      uint32_t fps_num, uint32_t fps_den,
                                      std::vector<IntelEncodedPacket>& packets,
                                      std::string& error) {
    if (bitstream.DataLength == 0) return MKVC_OK;
    const uint8_t* begin = bitstream.Data + bitstream.DataOffset;
    size_t offset = 0;
    size_t length = bitstream.DataLength;
    if (codec == MKVC_CODEC_VP9) {
        if (length >= 4 && std::memcmp(begin, "DKIF", 4) == 0) {
            if (length < 32) {
                error = "oneVPL returned a truncated VP9 IVF header";
                return MKVC_ERROR_CODEC;
            }
            offset = 32;
        }
        if (length < offset + 12) {
            error = "oneVPL returned a truncated VP9 IVF frame header";
            return MKVC_ERROR_CODEC;
        }
        const uint32_t frame_size = read_le32(begin + offset);
        offset += 12;
        if (frame_size == 0 || frame_size > length - offset) {
            error = "oneVPL returned an invalid VP9 IVF frame size";
            return MKVC_ERROR_CODEC;
        }
        length = frame_size;
    }
    IntelEncodedPacket packet;
    packet.data.assign(begin + offset, begin + offset + length);
    packet.pts = static_cast<int64_t>(bitstream.TimeStamp * fps_num / (90000ULL * fps_den));
    packet.key = (bitstream.FrameType & (MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR)) != 0;
    packets.push_back(std::move(packet));
    return MKVC_OK;
}

}  // namespace mkvc::gpu::intel
