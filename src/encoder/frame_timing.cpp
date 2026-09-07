#include "encoder/frame_timing.hpp"

#include <algorithm>

namespace mkvc::encoder {

void FrameTiming::configure(uint32_t fps_num, uint32_t fps_den) noexcept {
    fps_num_ = fps_num;
    fps_den_ = fps_den;
    next_pts_ = 0;
}

int64_t FrameTiming::select(int64_t requested_pts) const noexcept {
    return requested_pts >= 0 ? requested_pts : next_pts_;
}

void FrameTiming::commit(int64_t accepted_pts) noexcept {
    next_pts_ = std::max(next_pts_, accepted_pts + 1);
}

uint64_t FrameTiming::to_nanoseconds(int64_t pts) const noexcept {
    return static_cast<uint64_t>(pts) * fps_den_ * 1000000000ULL / fps_num_;
}

uint64_t FrameTiming::duration_nanoseconds() const noexcept {
    return static_cast<uint64_t>(fps_den_) * 1000000000ULL / fps_num_;
}

}  // namespace mkvc::encoder
