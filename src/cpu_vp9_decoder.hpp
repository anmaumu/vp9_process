#pragma once

#include "mkvcodec/mkvc.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mkvc {

/** Library-owned decoded I420 storage used behind mkvc_frame. */
struct DecodedFrame {
    uint32_t width = 0;                  /**< Visible frame width. */
    uint32_t height = 0;                 /**< Visible frame height. */
    int64_t pts_ns = 0;                  /**< Presentation timestamp in nanoseconds. */
    std::vector<uint8_t> pixels;         /**< Contiguous I420 allocation. */
    std::array<size_t, 3> offsets{};     /**< Y, U, V offsets into pixels. */
    std::array<int32_t, 3> strides{};    /**< Y, U, V row strides. */
};

/**
 * @brief Incremental libwebm demuxer and synchronous libvpx VP9 decoder.
 *
 * The decoder retains only submitted compressed packets and advances a
 * cluster/block/frame cursor instead of preloading the input file.
 */
class CpuVp9Decoder {
 public:
    /** Private parser and decoder state. */
    struct Impl;

    /** Open the input, select a VP9 track, and initialize libvpx. */
    static std::unique_ptr<CpuVp9Decoder> create(
        const mkvc_decoder_config& config, std::string& error);
    ~CpuVp9Decoder();

    CpuVp9Decoder(const CpuVp9Decoder&) = delete;
    CpuVp9Decoder& operator=(const CpuVp9Decoder&) = delete;

    /** Decode one frame or return MKVC_END_OF_STREAM. */
    mkvc_result read(std::unique_ptr<DecodedFrame>& frame, std::string& error);
    /** Release codec and parser resources idempotently. */
    mkvc_result close(std::string& error);

 private:
    CpuVp9Decoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
