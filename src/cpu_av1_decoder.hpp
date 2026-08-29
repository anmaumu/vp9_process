#pragma once

#include "cpu_vp9_decoder.hpp"

#include <memory>
#include <string>

namespace mkvc {

/** Incremental libwebm demuxer and synchronous libaom AV1 decoder. */
class CpuAv1Decoder {
 public:
    struct Impl;
    static std::unique_ptr<CpuAv1Decoder> create(
        const mkvc_decoder_config& config, std::string& error);
    ~CpuAv1Decoder();
    CpuAv1Decoder(const CpuAv1Decoder&) = delete;
    CpuAv1Decoder& operator=(const CpuAv1Decoder&) = delete;
    mkvc_result read(std::unique_ptr<DecodedFrame>& frame, std::string& error);
    mkvc_result close(std::string& error);

 private:
    CpuAv1Decoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
