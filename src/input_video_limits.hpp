/**
 * @file input_video_limits.hpp
 * @brief Fail-closed limits shared by container probe and packet extraction.
 */
#pragma once

#include <cstdint>

namespace mkvc::input_limits {

constexpr unsigned long kMaxTracks = 1024;
constexpr uint64_t kMaxDimension = 32768;
constexpr uint64_t kMaxPixels = 268435456;
constexpr uint64_t kMaxPacketBytes = 256ULL * 1024 * 1024;

/** Return whether decoded dimensions fit the supported allocation envelope. */
inline bool valid_dimensions(int64_t width, int64_t height) noexcept {
    if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) > kMaxDimension ||
        static_cast<uint64_t>(height) > kMaxDimension) {
        return false;
    }
    return static_cast<uint64_t>(width) <= kMaxPixels / static_cast<uint64_t>(height);
}

}  // namespace mkvc::input_limits
