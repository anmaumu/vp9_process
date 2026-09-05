/**
 * @file webm_muxer.hpp
 * @brief Backend-neutral WebM/Matroska video muxer.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc {

/**
 * @brief Own libwebm writer, segment, video-track, and container finalization.
 *
 * Codec backends provide encoded packet bytes, timestamps, durations, and
 * keyframe flags. This class applies the common VP9/AV1 track metadata and
 * final container DocType patching exactly once.
 */
class WebmMuxer {
   public:
    /**
     * @brief Create a single-track VP9 or AV1 output container.
     * @param path UTF-8 output path ending in .webm or .mkv.
     * @param codec VP9 or AV1 codec identifier.
     * @param width Visible frame width.
     * @param height Visible frame height.
     * @param fps_num Frame-rate numerator.
     * @param fps_den Frame-rate denominator.
     * @param error Receives a diagnostic on failure.
     * @return Owning muxer, or nullptr on failure.
     */
    static std::unique_ptr<WebmMuxer> create(const char* path, uint32_t codec, uint32_t width,
                                             uint32_t height, uint32_t fps_num, uint32_t fps_den,
                                             std::string& error);
    ~WebmMuxer();

    WebmMuxer(const WebmMuxer&) = delete;
    WebmMuxer& operator=(const WebmMuxer&) = delete;

    /**
     * @brief Copy one encoded packet into the output segment.
     * @param data Encoded packet bytes.
     * @param size Packet size in bytes.
     * @param timestamp_ns Presentation timestamp in nanoseconds.
     * @param duration_ns Display duration in nanoseconds.
     * @param key Whether the packet starts an independently decodable frame.
     * @param error Receives a diagnostic on failure.
     * @return MKVC_OK or an I/O/internal error.
     */
    mkvc_result add_frame(const uint8_t* data, size_t size, uint64_t timestamp_ns,
                          uint64_t duration_ns, bool key, std::string& error);

    /**
     * @brief Finalize the segment, close the file, and patch its DocType.
     * @param error Receives a diagnostic on failure.
     * @return MKVC_OK or MKVC_ERROR_IO.
     */
    mkvc_result finalize(std::string& error);

   private:
    struct Impl;
    WebmMuxer();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
