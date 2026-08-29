#pragma once

#include "intel_vpl_decoder.hpp"
#include "mkvcodec/mkvc.h"

#include <memory>
#include <string>

namespace mkvc {

/** Incremental libwebm demuxer backed by Intel oneVPL VP9/AV1 decode. */
class IntelWebmDecoder {
 public:
    struct Impl;
    static std::unique_ptr<IntelWebmDecoder> create(
        const mkvc_decoder_config& config, std::string& error);
    ~IntelWebmDecoder();
    IntelWebmDecoder(const IntelWebmDecoder&) = delete;
    IntelWebmDecoder& operator=(const IntelWebmDecoder&) = delete;
    mkvc_result read(std::unique_ptr<DecodedFrame>& frame, std::string& error);
    mkvc_result close(std::string& error);
    uint32_t max_pending_observed() const;

 private:
    IntelWebmDecoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
