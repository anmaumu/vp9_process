#pragma once

#include "mkvcodec/mkvc.h"

#include <memory>
#include <string>

namespace mkvc {

/**
 * @brief Synchronous libvpx VP9 encoder with libwebm output.
 *
 * Input frame bytes are copied or converted into an owned I420 buffer before
 * write() returns. close() drains libvpx and finalizes the container.
 */
class CpuVp9Encoder {
 public:
    /** Private implementation state kept out of the public translation unit. */
    struct Impl;

    /** Validate configuration and create an initialized encoder. */
    static std::unique_ptr<CpuVp9Encoder> create(
        const mkvc_encoder_config& config, std::string& error);
    ~CpuVp9Encoder();

    CpuVp9Encoder(const CpuVp9Encoder&) = delete;
    CpuVp9Encoder& operator=(const CpuVp9Encoder&) = delete;

    /** Convert/copy and encode one frame. */
    mkvc_result write(const mkvc_frame_view& frame, std::string& error);
    /** Drain delayed codec packets without finalizing the container. */
    mkvc_result flush(std::string& error);
    /** Drain and finalize all resources idempotently. */
    mkvc_result close(std::string& error);

 private:
    CpuVp9Encoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
