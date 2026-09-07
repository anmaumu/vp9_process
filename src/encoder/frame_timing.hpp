/**
 * @file frame_timing.hpp
 * @brief Shared frame-index timing for fixed-frame-rate encoders.
 */
#pragma once

#include <cstdint>

namespace mkvc::encoder {

/** Resolve fallback PTS values and convert frame ticks into nanoseconds. */
class FrameTiming final {
   public:
    /** Configure a nonzero fixed frame-rate rational and reset fallback PTS. */
    void configure(uint32_t fps_num, uint32_t fps_den) noexcept;

    /** Select an explicit PTS, or the next monotonic fallback when negative. */
    int64_t select(int64_t requested_pts) const noexcept;

    /** Advance the fallback PTS after a frame was accepted by the codec. */
    void commit(int64_t accepted_pts) noexcept;

    /** Convert one frame-index PTS into the container nanosecond timebase. */
    uint64_t to_nanoseconds(int64_t pts) const noexcept;

    /** Return the fixed display duration in nanoseconds. */
    uint64_t duration_nanoseconds() const noexcept;

    /** Return the configured frame-rate numerator. */
    uint32_t fps_num() const noexcept { return fps_num_; }

    /** Return the configured frame-rate denominator. */
    uint32_t fps_den() const noexcept { return fps_den_; }

   private:
    uint32_t fps_num_ = 1;
    uint32_t fps_den_ = 1;
    int64_t next_pts_ = 0;
};

}  // namespace mkvc::encoder
