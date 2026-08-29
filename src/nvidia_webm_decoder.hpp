#pragma once

#include "cpu_vp9_decoder.hpp"
#include "mkvcodec/mkvc.h"

#include <memory>
#include <string>

namespace mkvc {

/** Incremental libwebm demuxer backed by NVIDIA NVDEC VP9/AV1 decode. */
class NvidiaWebmDecoder {
public:
    struct Impl;
    static std::unique_ptr<NvidiaWebmDecoder> create(
        const mkvc_decoder_config& config, std::string& error);
    ~NvidiaWebmDecoder();
    NvidiaWebmDecoder(const NvidiaWebmDecoder&) = delete;
    NvidiaWebmDecoder& operator=(const NvidiaWebmDecoder&) = delete;
    mkvc_result read(std::unique_ptr<DecodedFrame>& frame, std::string& error);
    mkvc_result close(std::string& error);
    uint32_t max_pending_observed() const;

private:
    NvidiaWebmDecoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
