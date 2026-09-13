#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cpu_page_lock.hpp"
#include "mkvcodec/mkvc.h"

namespace mkvc {

class CpuFramePool;

/** One generation-checked lease over a fixed native CPU frame slot. */
class CpuBufferLease {
   public:
    ~CpuBufferLease();
    CpuBufferLease(const CpuBufferLease&) = delete;
    CpuBufferLease& operator=(const CpuBufferLease&) = delete;

    mkvc_result get_view(mkvc_mutable_frame_view& view, std::string& error) const;
    uint64_t generation() const noexcept { return generation_; }
    uint32_t pixel_format() const noexcept { return pixel_format_; }
    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    uint32_t plane_count() const noexcept { return plane_count_; }

   private:
    friend class CpuFramePool;
    CpuBufferLease(std::shared_ptr<CpuFramePool> pool, size_t slot, uint64_t generation,
                   uint32_t pixel_format, uint32_t width, uint32_t height, uint32_t plane_count);

    std::shared_ptr<CpuFramePool> pool_;
    size_t slot_ = 0;
    uint64_t generation_ = 0;
    uint32_t pixel_format_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t plane_count_ = 0;
    std::chrono::steady_clock::time_point acquired_at_{};
};

/** Fixed-capacity reusable native CPU frame allocation pool. */
class CpuFramePool : public std::enable_shared_from_this<CpuFramePool> {
   public:
    struct Slot {
        std::array<CpuAllocation, 4> planes;
        std::array<int32_t, 4> strides{};
        bool in_use = false;
        uint64_t generation = 0;
    };

    ~CpuFramePool();

    static mkvc_result create(const mkvc_cpu_frame_pool_config& config, uint32_t memory_mode,
                              std::shared_ptr<CpuFramePool>& pool, std::string& error);
    mkvc_result acquire(uint32_t timeout_ms, std::shared_ptr<CpuBufferLease>& lease,
                        std::string& error);
    mkvc_result get_view(size_t slot, uint64_t generation, mkvc_mutable_frame_view& view,
                         std::string& error) const;
    void recycle(size_t slot, uint64_t generation,
                 std::chrono::steady_clock::time_point acquired_at) noexcept;
    uint32_t capacity() const noexcept;
    void snapshot_stats(mkvc_cpu_frame_pool_stats& stats) const noexcept;

   private:
    CpuFramePool(uint32_t pixel_format, uint32_t width, uint32_t height, uint32_t plane_count,
                 uint32_t memory_mode, uint64_t allocation_bytes, uint64_t page_locked_bytes,
                 std::vector<Slot> slots);

    uint32_t pixel_format_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t plane_count_ = 0;
    uint32_t memory_mode_ = MKVC_CPU_MEMORY_PAGEABLE;
    uint64_t allocation_bytes_ = 0;
    uint64_t page_locked_bytes_ = 0;
    uint32_t in_use_ = 0;
    uint32_t peak_in_use_ = 0;
    uint64_t acquisitions_ = 0;
    uint64_t rejected_acquisitions_ = 0;
    uint64_t wait_ns_ = 0;
    uint64_t lease_time_ns_ = 0;
    uint64_t peak_lease_time_ns_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::vector<Slot> slots_;
};

}  // namespace mkvc
