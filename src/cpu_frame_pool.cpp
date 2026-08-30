#include "cpu_frame_pool.hpp"

#include <chrono>
#include <limits>
#include <utility>

namespace mkvc {
namespace {

bool configure_slot(uint32_t format, uint32_t width, uint32_t height,
                    CpuFramePool::Slot& slot, uint32_t& plane_count,
                    std::string& error) {
    auto allocate = [&slot](size_t index, uint32_t row_bytes, uint32_t rows) {
        slot.strides[index] = static_cast<int32_t>(row_bytes);
        slot.planes[index].resize(static_cast<size_t>(row_bytes) * rows);
    };
    switch (format) {
        case MKVC_PIXEL_FORMAT_I420:
            plane_count = 3;
            allocate(0, width, height);
            allocate(1, width / 2, height / 2);
            allocate(2, width / 2, height / 2);
            return true;
        case MKVC_PIXEL_FORMAT_NV12:
            plane_count = 2;
            allocate(0, width, height);
            allocate(1, width, height / 2);
            return true;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
            plane_count = 1;
            allocate(0, width * 3, height);
            return true;
        case MKVC_PIXEL_FORMAT_BGRA32:
            plane_count = 1;
            allocate(0, width * 4, height);
            return true;
        default:
            error = "unsupported native CPU pool pixel format";
            return false;
    }
}

}  // namespace

CpuBufferLease::CpuBufferLease(
    std::shared_ptr<CpuFramePool> pool, size_t slot, uint64_t generation,
    uint32_t pixel_format, uint32_t width, uint32_t height,
    uint32_t plane_count)
    : pool_(std::move(pool)), slot_(slot), generation_(generation),
      pixel_format_(pixel_format), width_(width), height_(height),
      plane_count_(plane_count) {}

CpuBufferLease::~CpuBufferLease() {
    if (pool_) pool_->recycle(slot_, generation_);
}

mkvc_result CpuBufferLease::get_view(mkvc_mutable_frame_view& view,
                                     std::string& error) const {
    if (!pool_) {
        error = "native CPU buffer lease is released";
        return MKVC_ERROR_INVALID_STATE;
    }
    return pool_->get_view(slot_, generation_, view, error);
}

CpuFramePool::CpuFramePool(uint32_t pixel_format, uint32_t width,
                           uint32_t height, uint32_t plane_count,
                           std::vector<Slot> slots)
    : pixel_format_(pixel_format), width_(width), height_(height),
      plane_count_(plane_count), slots_(std::move(slots)) {}

std::shared_ptr<CpuFramePool> CpuFramePool::create(
    const mkvc_cpu_frame_pool_config& config, std::string& error) {
    if (config.capacity == 0 || config.width == 0 || config.height == 0 ||
        (config.width & 1u) != 0 || (config.height & 1u) != 0) {
        error = "CPU pool capacity and even dimensions must be positive";
        return nullptr;
    }
    if (config.width >
        static_cast<uint32_t>(std::numeric_limits<int32_t>::max() / 4)) {
        error = "CPU pool row stride exceeds the ABI int32 limit";
        return nullptr;
    }
    std::vector<Slot> slots(config.capacity);
    uint32_t plane_count = 0;
    for (auto& slot : slots) {
        if (!configure_slot(config.pixel_format, config.width, config.height,
                            slot, plane_count, error)) return nullptr;
    }
    return std::shared_ptr<CpuFramePool>(new CpuFramePool(
        config.pixel_format, config.width, config.height, plane_count,
        std::move(slots)));
}

mkvc_result CpuFramePool::acquire(uint32_t timeout_ms,
                                  std::shared_ptr<CpuBufferLease>& lease,
                                  std::string& error) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto free_slot = [this] {
        for (const auto& slot : slots_) if (!slot.in_use) return true;
        return false;
    };
    if (timeout_ms == std::numeric_limits<uint32_t>::max()) {
        available_.wait(lock, free_slot);
    } else if (!available_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                    free_slot)) {
        error = "native CPU frame pool acquire timed out";
        return timeout_ms == 0 ? MKVC_WOULD_BLOCK : MKVC_ERROR_TIMEOUT;
    }
    for (size_t index = 0; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        if (slot.in_use) continue;
        slot.in_use = true;
        ++slot.generation;
        lease = std::shared_ptr<CpuBufferLease>(new CpuBufferLease(
            shared_from_this(), index, slot.generation, pixel_format_, width_,
            height_, plane_count_));
        return MKVC_OK;
    }
    error = "native CPU frame pool has no free slot";
    return MKVC_WOULD_BLOCK;
}

mkvc_result CpuFramePool::get_view(size_t slot_index, uint64_t generation,
                                   mkvc_mutable_frame_view& view,
                                   std::string& error) const {
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

void CpuFramePool::recycle(size_t slot_index, uint64_t generation) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_index >= slots_.size()) return;
    auto& slot = slots_[slot_index];
    if (!slot.in_use || slot.generation != generation) return;
    slot.in_use = false;
    available_.notify_one();
}

uint32_t CpuFramePool::capacity() const noexcept {
    return static_cast<uint32_t>(slots_.size());
}

}  // namespace mkvc
