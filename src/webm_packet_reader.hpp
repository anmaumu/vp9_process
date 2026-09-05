/**
 * @file webm_packet_reader.hpp
 * @brief Backend-neutral incremental WebM/Matroska packet reader.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mkvcodec/mkvc.h"

namespace mkvc {

/** @brief One compressed video frame and its nanosecond presentation time. */
struct EncodedPacket {
    std::vector<uint8_t> data; /**< Exact compressed frame bytes. */
    int64_t pts_ns = 0;        /**< Presentation timestamp in nanoseconds. */
};

/**
 * @brief Incrementally read one VP9 or AV1 video track through libwebm.
 *
 * The reader owns the parser, segment, and cluster cursor. It validates the
 * container declaration, selects exactly the requested codec track, limits a
 * compressed frame to 256 MiB, and never preloads the media payload.
 */
class WebmPacketReader {
   public:
    /**
     * @brief Open a WebM/Matroska input and select its requested video track.
     * @param path UTF-8 filesystem path.
     * @param codec Requested VP9 or AV1 codec.
     * @param error Receives a diagnostic when creation fails.
     * @return Owning reader, or nullptr on failure.
     */
    static std::unique_ptr<WebmPacketReader> open(const char* path, uint32_t codec,
                                                  std::string& error);
    ~WebmPacketReader();

    WebmPacketReader(const WebmPacketReader&) = delete;
    WebmPacketReader& operator=(const WebmPacketReader&) = delete;

    /**
     * @brief Read the next compressed frame from the selected track.
     * @param packet Receives bytes and timestamp on MKVC_OK.
     * @param error Receives a diagnostic on failure.
     * @return MKVC_OK, MKVC_END_OF_STREAM, or an error result.
     */
    mkvc_result read(EncodedPacket& packet, std::string& error);

   private:
    struct Impl;
    WebmPacketReader();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
