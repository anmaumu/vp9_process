#pragma once

#include "mkvcodec/mkvc.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mkvc {

/** One compressed frame returned by the oneVPL encode adapter. */
struct IntelEncodedPacket {
    std::vector<uint8_t> data;
    int64_t pts = 0;
    bool key = false;
};

/** oneVPL hardware encoder using API 2.x internally managed NV12 surfaces. */
class IntelVplEncoder {
 public:
    struct Impl;
    static std::unique_ptr<IntelVplEncoder> create(
        uint32_t codec, uint32_t width, uint32_t height,
        uint32_t fps_num, uint32_t fps_den, uint32_t quality,
        uint32_t keyframe_interval_frames, std::string& error,
        uint32_t async_depth = 4);
    ~IntelVplEncoder();
    IntelVplEncoder(const IntelVplEncoder&) = delete;
    IntelVplEncoder& operator=(const IntelVplEncoder&) = delete;

    /** Copy one NV12 frame and return any completed packets. */
    mkvc_result write_nv12(const uint8_t* y, int32_t y_stride,
                           const uint8_t* uv, int32_t uv_stride,
                           int64_t pts, std::vector<IntelEncodedPacket>& packets,
                           std::string& error);
    /** Drain all delayed packets without destroying the adapter. */
    mkvc_result drain(std::vector<IntelEncodedPacket>& packets,
                      std::string& error);
    /** Release encoder, session, and dispatcher resources idempotently. */
    mkvc_result close(std::string& error);
    /** Largest number of simultaneously outstanding SyncPoints observed. */
    uint32_t max_pending_observed() const;

 private:
    IntelVplEncoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
