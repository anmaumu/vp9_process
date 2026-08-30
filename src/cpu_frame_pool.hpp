#pragma once

#include "mkvcodec/mkvc.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mkvc {

class CpuFramePool;

/** One generation-checked lease over a fixed native CPU frame slot. */
class CpuBufferLease {
 public:
    ~CpuBufferLease();
    CpuBufferLease(const CpuBufferLease&) = delete;
    CpuBufferLease& operator=(const CpuBufferLease&) = delete;

    mkvc_result get_view(mkvc_mutable_frame_view& view,
                         std::string& error) const;
    uint64_t generation() const noexcept { return generation_; }
    uint32_t pixel_format() const noexcept { return pixel_format_; }
    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    uint32_t plane_count() const noexcept { return plane_count_; }

 private:
    friend class CpuFramePool;
    CpuBufferLease(std::shared_ptr<CpuFramePool> pool, size_t slot,
                   uint64_t generation, uint32_t pixel_format,
                   uint32_t width, uint32_t height, uint32_t plane_count);

    std::shared_ptr<CpuFramePool> pool_;
    size_t slot_ = 0;
    uint64_t generation_ = 0;
    uint32_t pixel_format_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t plane_count_ = 0;
};

/** Fixed-capacity reusable native CPU frame allocation pool. */
class CpuFramePool : public std::enable_shared_from_this<CpuFramePool> {
 public:
    struct Slot {
        std::array<std::vector<uint8_t>, 4> planes;
        std::array<int32_t, 4> strides{};
        bool in_use = false;
        uint64_t generation = 0;
    };

    static std::shared_ptr<CpuFramePool> create(
        const mkvc_cpu_frame_pool_config& config, std::string& error);
    mkvc_result acquire(uint32_t timeout_ms,
                        std::shared_ptr<CpuBufferLease>& lease,
                        std::string& error);
    mkvc_result get_view(size_t slot, uint64_t generation,
                         mkvc_mutable_frame_view& view,
                         std::string& error) const;
    void recycle(size_t slot, uint64_t generation) noexcept;
    uint32_t capacity() const noexcept;

 private:
    CpuFramePool(uint32_t pixel_format, uint32_t width, uint32_t height,
                 uint32_t plane_count, std::vector<Slot> slots);

    uint32_t pixel_format_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t plane_count_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::vector<Slot> slots_;
};

}  // namespace mkvc
