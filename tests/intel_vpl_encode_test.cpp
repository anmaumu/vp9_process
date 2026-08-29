#include "intel_vpl_encoder.hpp"

#include <aom/aom_decoder.h>
#include <aom/aomdx.h>
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kWidth = 320;
constexpr uint32_t kHeight = 240;
constexpr uint32_t kFrames = 30;

bool encode(uint32_t codec, std::vector<mkvc::IntelEncodedPacket>& output,
            std::string& error) {
    auto encoder = mkvc::IntelVplEncoder::create(
        codec, kWidth, kHeight, 30, 1, 32, 0, error);
    if (!encoder) return false;
    std::vector<uint8_t> y(kWidth * kHeight);
    std::vector<uint8_t> uv(kWidth * kHeight / 2, 128);
    for (uint32_t index = 0; index < kFrames; ++index) {
        for (uint32_t row = 0; row < kHeight; ++row) {
            for (uint32_t column = 0; column < kWidth; ++column) {
                y[row * kWidth + column] = static_cast<uint8_t>(
                    (column * 3 + row * 2 + index * 7) & 0xff);
            }
        }
        std::vector<mkvc::IntelEncodedPacket> completed;
        if (encoder->write_nv12(y.data(), kWidth, uv.data(), kWidth, -1,
                                completed, error) != MKVC_OK) {
            return false;
        }
        for (auto& packet : completed) output.push_back(std::move(packet));
    }
    std::vector<mkvc::IntelEncodedPacket> drained;
    if (encoder->drain(drained, error) != MKVC_OK) return false;
    for (auto& packet : drained) output.push_back(std::move(packet));
    encoder->close(error);
    return true;
}

uint32_t decode_vp9(const std::vector<mkvc::IntelEncodedPacket>& packets) {
    vpx_codec_ctx_t decoder{};
    if (vpx_codec_dec_init(&decoder, vpx_codec_vp9_dx(), nullptr, 0) !=
        VPX_CODEC_OK) return 0;
    uint32_t count = 0;
    for (const auto& packet : packets) {
        if (vpx_codec_decode(&decoder, packet.data.data(),
                             static_cast<unsigned int>(packet.data.size()),
                             nullptr, 0) != VPX_CODEC_OK) {
            std::cerr << "libvpx decode error: " << vpx_codec_error(&decoder);
            if (vpx_codec_error_detail(&decoder)) {
                std::cerr << " (" << vpx_codec_error_detail(&decoder) << ")";
            }
            std::cerr << " bytes=" << packet.data.size() << '\n';
            vpx_codec_destroy(&decoder);
            return 0;
        }
        vpx_codec_iter_t iterator = nullptr;
        while (vpx_codec_get_frame(&decoder, &iterator) != nullptr) ++count;
    }
    vpx_codec_destroy(&decoder);
    return count;
}

uint32_t decode_av1(const std::vector<mkvc::IntelEncodedPacket>& packets) {
    aom_codec_ctx_t decoder{};
    if (aom_codec_dec_init(&decoder, aom_codec_av1_dx(), nullptr, 0) !=
        AOM_CODEC_OK) return 0;
    uint32_t count = 0;
    for (const auto& packet : packets) {
        if (aom_codec_decode(&decoder, packet.data.data(), packet.data.size(),
                             nullptr) != AOM_CODEC_OK) {
            aom_codec_destroy(&decoder);
            return 0;
        }
        aom_codec_iter_t iterator = nullptr;
        while (aom_codec_get_frame(&decoder, &iterator) != nullptr) ++count;
    }
    aom_codec_destroy(&decoder);
    return count;
}

}  // namespace

int main() {
    std::string error;
    std::vector<mkvc::IntelEncodedPacket> vp9;
    if (!encode(MKVC_CODEC_VP9, vp9, error)) {
        std::cout << "Intel encode unavailable: " << error << '\n';
        return std::getenv("MKVC_REQUIRE_INTEL_GPU") != nullptr ? 1 : 77;
    }
    std::vector<mkvc::IntelEncodedPacket> av1;
    if (!encode(MKVC_CODEC_AV1, av1, error)) {
        std::cerr << "Intel AV1 encode failed: " << error << '\n';
        return 1;
    }
    const uint32_t vp9_decoded = decode_vp9(vp9);
    const uint32_t av1_decoded = decode_av1(av1);
    bool timestamps_ok = vp9.size() == kFrames && av1.size() == kFrames;
    for (uint32_t index = 0; timestamps_ok && index < kFrames; ++index) {
        timestamps_ok = timestamps_ok && vp9[index].pts == index &&
                        av1[index].pts == index;
    }
    const bool first_frames_are_key = !vp9.empty() && !av1.empty() &&
                                      vp9.front().key && av1.front().key;
    if (vp9.size() != kFrames || av1.size() != kFrames ||
        !first_frames_are_key ||
        vp9_decoded != kFrames || av1_decoded != kFrames || !timestamps_ok) {
        std::cerr << "Intel packet/decode validation failed: vp9=" << vp9.size()
                  << "/" << vp9_decoded << " key=" << first_frames_are_key
                  << " av1=" << av1.size() << "/" << av1_decoded
                  << '\n';
        return 1;
    }
    std::cout << "Intel oneVPL VP9/AV1 encoded and independently decoded "
              << kFrames << " frames each\n";
    return 0;
}
