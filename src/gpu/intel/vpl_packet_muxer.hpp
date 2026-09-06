/**
 * @file vpl_packet_muxer.hpp
 * @brief oneVPL packet timestamp conversion and WebM/Matroska muxing.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc {
struct IntelEncodedPacket;
class WebmMuxer;
namespace gpu::intel {

/** Convert frame-index oneVPL timestamps and write encoded packets in order. */
class VplPacketMuxer final {
   public:
    /** Create the backend-neutral container muxer and fixed frame timing. */
    static std::unique_ptr<VplPacketMuxer> create(const mkvc_encoder_config& config,
                                                  std::string& error);
    ~VplPacketMuxer();
    VplPacketMuxer(const VplPacketMuxer&) = delete;
    VplPacketMuxer& operator=(const VplPacketMuxer&) = delete;

    /** Append every packet using its frame-index PTS converted to nanoseconds. */
    mkvc_result write(const std::vector<IntelEncodedPacket>& packets, std::string& error);

    /** Finalize the output container idempotently. */
    mkvc_result finalize(std::string& error);

   private:
    VplPacketMuxer() = default;
    std::unique_ptr<WebmMuxer> muxer_;
    uint32_t fps_num_ = 0;
    uint32_t fps_den_ = 0;
};

}  // namespace gpu::intel
}  // namespace mkvc
