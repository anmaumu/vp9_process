#pragma once

#include "mkvcodec/mkvc.h"

#include <memory>
#include <string>

namespace mkvc {

class CpuVp9Encoder {
 public:
    struct Impl;

    static std::unique_ptr<CpuVp9Encoder> create(
        const mkvc_encoder_config& config, std::string& error);
    ~CpuVp9Encoder();

    CpuVp9Encoder(const CpuVp9Encoder&) = delete;
    CpuVp9Encoder& operator=(const CpuVp9Encoder&) = delete;

    mkvc_result write(const mkvc_frame_view& frame, std::string& error);
    mkvc_result flush(std::string& error);
    mkvc_result close(std::string& error);

 private:
    CpuVp9Encoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
