#pragma once

#include "mkvcodec/mkvc.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mkvc {

struct DecodedFrame {
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t pts_ns = 0;
    std::vector<uint8_t> pixels;
    std::array<size_t, 3> offsets{};
    std::array<int32_t, 3> strides{};
};

class CpuVp9Decoder {
 public:
    struct Impl;

    static std::unique_ptr<CpuVp9Decoder> create(
        const mkvc_decoder_config& config, std::string& error);
    ~CpuVp9Decoder();

    CpuVp9Decoder(const CpuVp9Decoder&) = delete;
    CpuVp9Decoder& operator=(const CpuVp9Decoder&) = delete;

    mkvc_result read(std::unique_ptr<DecodedFrame>& frame, std::string& error);
    mkvc_result close(std::string& error);

 private:
    CpuVp9Decoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
