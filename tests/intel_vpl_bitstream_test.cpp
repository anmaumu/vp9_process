#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "gpu/intel/vpl_bitstream.hpp"
#include "intel_vpl_encoder.hpp"

namespace {

mfxBitstream bitstream(std::vector<uint8_t>& bytes, mfxU32 offset = 0) {
    mfxBitstream value{};
    value.Data = bytes.data();
    value.DataOffset = offset;
    value.DataLength = static_cast<mfxU32>(bytes.size() - offset);
    value.MaxLength = static_cast<mfxU32>(bytes.size());
    value.TimeStamp = 90000;
    value.FrameType = MFX_FRAMETYPE_I;
    return value;
}

}  // namespace

int main() {
    std::string error;
    std::vector<mkvc::IntelEncodedPacket> packets;

    std::vector<uint8_t> av1{0xff, 1, 2, 3, 4};
    auto av1_output = bitstream(av1, 1);
    assert(mkvc::gpu::intel::append_vpl_encoded_packet(av1_output, MKVC_CODEC_AV1, 30, 1, packets,
                                                       error) == MKVC_OK);
    assert(packets.size() == 1 && packets[0].data == std::vector<uint8_t>({1, 2, 3, 4}));
    assert(packets[0].pts == 30 && packets[0].key);

    std::vector<uint8_t> vp9(32 + 12 + 3, 0);
    vp9[0] = 'D';
    vp9[1] = 'K';
    vp9[2] = 'I';
    vp9[3] = 'F';
    vp9[32] = 3;
    vp9[44] = 9;
    vp9[45] = 8;
    vp9[46] = 7;
    auto vp9_output = bitstream(vp9);
    assert(mkvc::gpu::intel::append_vpl_encoded_packet(vp9_output, MKVC_CODEC_VP9, 60, 1, packets,
                                                       error) == MKVC_OK);
    assert(packets.size() == 2 && packets[1].data == std::vector<uint8_t>({9, 8, 7}));
    assert(packets[1].pts == 60);

    std::vector<uint8_t> truncated(16, 0);
    truncated[0] = 'D';
    truncated[1] = 'K';
    truncated[2] = 'I';
    truncated[3] = 'F';
    auto invalid_output = bitstream(truncated);
    assert(mkvc::gpu::intel::append_vpl_encoded_packet(invalid_output, MKVC_CODEC_VP9, 30, 1,
                                                       packets, error) == MKVC_ERROR_CODEC);
    assert(packets.size() == 2);

    std::vector<uint8_t> oversized(14, 0);
    oversized[0] = 20;
    invalid_output = bitstream(oversized);
    assert(mkvc::gpu::intel::append_vpl_encoded_packet(invalid_output, MKVC_CODEC_VP9, 30, 1,
                                                       packets, error) == MKVC_ERROR_CODEC);
    assert(packets.size() == 2);
    return 0;
}
