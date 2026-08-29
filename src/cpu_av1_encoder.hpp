#pragma once

#include "mkvcodec/mkvc.h"

#include <memory>
#include <string>

namespace mkvc {

/** CPU AV1 encoder backed by SVT-AV1 and a libwebm muxer. */
class CpuAv1Encoder {
 public:
    struct Impl;
    static std::unique_ptr<CpuAv1Encoder> create(
        const mkvc_encoder_config& config, std::string& error);
    ~CpuAv1Encoder();
    CpuAv1Encoder(const CpuAv1Encoder&) = delete;
    CpuAv1Encoder& operator=(const CpuAv1Encoder&) = delete;
    mkvc_result write(const mkvc_frame_view& frame, std::string& error);
    mkvc_result flush(std::string& error);
    mkvc_result close(std::string& error);

 private:
    CpuAv1Encoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
