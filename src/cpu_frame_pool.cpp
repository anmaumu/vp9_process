#include "cpu_frame_pool.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

#include "cpu_page_lock.hpp"

namespace mkvc {
namespace {

bool configure_slot(uint32_t format, uint32_t width, uint32_t height, bool page_locked,
                    CpuFramePool::Slot& slot, uint32_t& plane_count, std::string& error) {
    auto allocate = [&slot, page_locked, &error](size_t index, uint32_t row_bytes, uint32_t rows) {
        slot.strides[index] = static_cast<int32_t>(row_bytes);
        return slot.planes[index].allocate(static_cast<size_t>(row_bytes) * rows, page_locked,
                                           error);
    };
    switch (format) {
        case MKVC_PIXEL_FORMAT_I420:
            plane_count = 3;
            return allocate(0, width, height) && allocate(1, width / 2, height / 2) &&
                   allocate(2, width / 2, height / 2);
        case MKVC_PIXEL_FORMAT_NV12:
            plane_count = 2;
            return allocate(0, width, height) && allocate(1, width, height / 2);
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
            plane_count = 1;
            return allocate(0, width * 3, height);
        case MKVC_PIXEL_FORMAT_BGRA32:
            plane_count = 1;
            return allocate(0, width * 4, height);
        default:
            error = "unsupported native CPU pool pixel format";
            return false;
    }
}

}  // namespace

CpuBufferLease::CpuBufferLease(std::shared_ptr<CpuFramePool> pool, size_t slot, uint64_t generation,
                               uint32_t pixel_format, uint32_t width, uint32_t height,
                               uint32_t plane_count)
    : pool_(std::move(pool)),
      slot_(slot),
      generation_(generation),
      pixel_format_(pixel_format),
      width_(width),
      height_(height),
      plane_count_(plane_count),
      acquired_at_(std::chrono::steady_clock::now()) {}

CpuBufferLease::~CpuBufferLease() {
    if (pool_) pool_->recycle(slot_, generation_, acquired_at_);
}

mkvc_result CpuBufferLease::get_view(mkvc_mutable_frame_view& view, std::string& error) const {
    if (!pool_) {
        error = "native CPU buffer lease is released";
        return MKVC_ERROR_INVALID_STATE;
    }
    return pool_->get_view(slot_, generation_, view, error);
}

CpuFramePool::CpuFramePool(uint32_t pixel_format, uint32_t width, uint32_t height,
                           uint32_t plane_count, uint32_t memory_mode, uint64_t allocation_bytes,
                           uint64_t page_locked_bytes, std::vector<Slot> slots)
    : pixel_format_(pixel_format),
      width_(width),
      height_(height),
      plane_count_(plane_count),
      memory_mode_(memory_mode),
      allocation_bytes_(allocation_bytes),
      page_locked_bytes_(page_locked_bytes),
      slots_(std::move(slots)) {}

CpuFramePool::~CpuFramePool() = default;

mkvc_result CpuFramePool::create(const mkvc_cpu_frame_pool_config& config, uint32_t memory_mode,
                                 std::shared_ptr<CpuFramePool>& pool, std::string& error) {
    pool.reset();
    if (config.capacity == 0 || config.width == 0 || config.height == 0 ||
        (config.width & 1u) != 0 || (config.height & 1u) != 0) {
        error = "CPU pool capacity and even dimensions must be positive";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    if (memory_mode != MKVC_CPU_MEMORY_PAGEABLE && memory_mode != MKVC_CPU_MEMORY_PAGE_LOCKED) {
        error = "unsupported CPU pool memory mode";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    if (config.width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max() / 4)) {
        error = "CPU pool row stride exceeds the ABI int32 limit";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    if (config.pixel_format != MKVC_PIXEL_FORMAT_I420 &&
        config.pixel_format != MKVC_PIXEL_FORMAT_NV12 &&
        config.pixel_format != MKVC_PIXEL_FORMAT_BGR24 &&
        config.pixel_format != MKVC_PIXEL_FORMAT_RGB24 &&
        config.pixel_format != MKVC_PIXEL_FORMAT_BGRA32) {
        error = "unsupported native CPU pool pixel format";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    std::vector<Slot> slots(config.capacity);
    uint32_t plane_count = 0;
    for (auto& slot : slots) {
        if (!configure_slot(config.pixel_format, config.width, config.height,
                            memory_mode == MKVC_CPU_MEMORY_PAGE_LOCKED, slot, plane_count, error))
            return memory_mode == MKVC_CPU_MEMORY_PAGE_LOCKED ? MKVC_ERROR_NOT_SUPPORTED
                                                              : MKVC_ERROR_INVALID_ARGUMENT;
    }
    uint64_t allocation_bytes = 0;
    uint64_t page_locked_bytes = 0;
    for (const auto& slot : slots)
        for (const auto& plane : slot.planes) {
            allocation_bytes += plane.size();
            page_locked_bytes += plane.page_locked_bytes();
        }
    pool = std::shared_ptr<CpuFramePool>(
        new CpuFramePool(config.pixel_format, config.width, config.height, plane_count, memory_mode,
                         allocation_bytes, page_locked_bytes, std::move(slots)));
    return MKVC_OK;
}

mkvc_result CpuFramePool::acquire(uint32_t timeout_ms, std::shared_ptr<CpuBufferLease>& lease,
                                  std::string& error) {
    const auto started = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(mutex_);
    const auto free_slot = [this] {
        for (const auto& slot : slots_)
            if (!slot.in_use) return true;
        return false;
    };
    if (timeout_ms == std::numeric_limits<uint32_t>::max()) {
        available_.wait(lock, free_slot);
    } else if (!available_.wait_for(lock, std::chrono::milliseconds(timeout_ms), free_slot)) {
        wait_ns_ += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - started)
                                              .count());
        ++rejected_acquisitions_;
        error = "native CPU frame pool acquire timed out";
        return timeout_ms == 0 ? MKVC_WOULD_BLOCK : MKVC_ERROR_TIMEOUT;
    }
    for (size_t index = 0; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        if (slot.in_use) continue;
        const uint64_t next_generation = slot.generation + 1;
        auto new_lease = std::shared_ptr<CpuBufferLease>(
            new CpuBufferLease(shared_from_this(), index, next_generation, pixel_format_, width_,
                               height_, plane_count_));
        slot.in_use = true;
        slot.generation = next_generation;
        wait_ns_ += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - started)
                                              .count());
        ++acquisitions_;
        ++in_use_;
        peak_in_use_ = std::max(peak_in_use_, in_use_);
        lease = std::move(new_lease);
        return MKVC_OK;
    }
    error = "native CPU frame pool has no free slot";
    ++rejected_acquisitions_;
    return MKVC_WOULD_BLOCK;
}

mkvc_result CpuFramePool::get_view(size_t slot_index, uint64_t generation,
                                   mkvc_mutable_frame_view& view, std::string& error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_index >= slots_.size()) {
        error = "native CPU buffer slot is invalid";
        return MKVC_ERROR_INVALID_STATE;
    }
    const auto& slot = slots_[slot_index];
    if (!slot.in_use || slot.generation != generation) {
        error = "native CPU buffer generation is stale";
        return MKVC_ERROR_INVALID_STATE;
    }
    view = {};
    view.struct_size = sizeof(view);
    view.struct_version = 1;
    view.pixel_format = pixel_format_;
    view.width = width_;
    view.height = height_;
    for (size_t index = 0; index < plane_count_; ++index) {
        view.planes[index] = const_cast<uint8_t*>(slot.planes[index].data());
        view.strides[index] = slot.strides[index];
    }
    view.pts = -1;
    return MKVC_OK;
}

void CpuFramePool::recycle(size_t slot_index, uint64_t generation,
                           std::chrono::steady_clock::time_point acquired_at) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_index >= slots_.size()) return;
    auto& slot = slots_[slot_index];
    if (!slot.in_use || slot.generation != generation) return;
    slot.in_use = false;
    if (in_use_ != 0) --in_use_;
    const uint64_t lease_ns =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now() - acquired_at)
                                  .count());
    lease_time_ns_ += lease_ns;
    peak_lease_time_ns_ = std::max(peak_lease_time_ns_, lease_ns);
    available_.notify_one();
}

uint32_t CpuFramePool::capacity() const noexcept { return static_cast<uint32_t>(slots_.size()); }

void CpuFramePool::snapshot_stats(mkvc_cpu_frame_pool_stats& stats) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    stats = {};
    stats.struct_size = sizeof(stats);
    stats.struct_version = 1;
    stats.capacity = capacity();
    stats.in_use = in_use_;
    stats.peak_in_use = peak_in_use_;
    stats.memory_mode = memory_mode_;
    stats.allocation_bytes = allocation_bytes_;
    stats.page_locked_bytes = page_locked_bytes_;
    stats.acquisitions = acquisitions_;
    stats.rejected_acquisitions = rejected_acquisitions_;
    stats.wait_ns = wait_ns_;
    stats.lease_time_ns = lease_time_ns_;
    stats.peak_lease_time_ns = peak_lease_time_ns_;
}

}  // namespace mkvc
