#pragma once

#include "intel_vpl_encoder.hpp"
#include "mkvcodec/mkvc.h"

#include <memory>
#include <string>

namespace mkvc {

namespace gpu { class GpuFrameCore; }

/** oneVPL VP9/AV1 encoder with CPU input conversion and libwebm muxing. */
class IntelWebmEncoder {
 public:
    struct Impl;
    static std::unique_ptr<IntelWebmEncoder> create(
        const mkvc_encoder_config& config, std::string& error);
    ~IntelWebmEncoder();
    IntelWebmEncoder(const IntelWebmEncoder&) = delete;
    IntelWebmEncoder& operator=(const IntelWebmEncoder&) = delete;
    mkvc_result write(const mkvc_frame_view& frame, std::string& error);
    /** Encode an Intel decoder surface without copying pixels through CPU memory. */
    mkvc_result write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                          std::string& error);
    mkvc_result flush(std::string& error);
    mkvc_result close(std::string& error);
    uint32_t max_pending_observed() const;

 private:
    IntelWebmEncoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
