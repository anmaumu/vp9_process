#include "gpu_resource_pool.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

namespace mkvc::gpu {

GpuResourcePool::GpuResourcePool(uint32_t capacity) : slots_(capacity) {}

mkvc_result GpuResourcePool::acquire(uint32_t timeout_ms,
                                     std::shared_ptr<GpuResourceReservation>& output,
                                     std::string& error) {
    output.reset();
    std::unique_lock<std::mutex> lock(mutex_);
    const auto available = [this] {
        return std::any_of(slots_.begin(), slots_.end(),
                           [](const Slot& slot) { return !slot.in_use; });
    };
    if (!available()) {
        const auto wait_started = std::chrono::steady_clock::now();
        if (timeout_ms == 0) {
            ++rejected_acquisitions_;
            error = "external GPU resource pool is full";
            return MKVC_WOULD_BLOCK;
        }
        if (timeout_ms == std::numeric_limits<uint32_t>::max()) {
            changed_.wait(lock, available);
        } else if (!changed_.wait_for(lock, std::chrono::milliseconds(timeout_ms), available)) {
            wait_ns_ += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                  std::chrono::steady_clock::now() - wait_started)
                                                  .count());
            ++rejected_acquisitions_;
            error = "external GPU resource pool acquire timed out";
            return MKVC_ERROR_TIMEOUT;
        }
        wait_ns_ += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - wait_started)
                                              .count());
    }
    const auto iterator =
        std::find_if(slots_.begin(), slots_.end(), [](const Slot& slot) { return !slot.in_use; });
    const uint32_t index = static_cast<uint32_t>(iterator - slots_.begin());
    iterator->in_use = true;
    if (++iterator->generation == 0) ++iterator->generation;
    const uint64_t generation = iterator->generation;
    ++in_use_;
    ++acquisitions_;
    peak_in_use_ = std::max(peak_in_use_, in_use_);
    lock.unlock();
    try {
        output = std::make_shared<GpuResourceReservation>(shared_from_this(), index, generation);
        return MKVC_OK;
    } catch (...) {
        release(index, generation);
        throw;
    }
}

GpuResourcePool::Snapshot GpuResourcePool::snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return {static_cast<uint32_t>(slots_.size()),
            in_use_,
            peak_in_use_,
            acquisitions_,
            rejected_acquisitions_,
            wait_ns_};
}

void GpuResourcePool::release(uint32_t slot, uint64_t generation) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (slot >= slots_.size()) return;
        auto& candidate = slots_[slot];
        if (!candidate.in_use || candidate.generation != generation) return;
        candidate.in_use = false;
        if (in_use_ != 0) --in_use_;
    }
    changed_.notify_one();
}

GpuResourceReservation::GpuResourceReservation(std::shared_ptr<GpuResourcePool> pool, uint32_t slot,
                                               uint64_t generation) noexcept
    : pool_(std::move(pool)), slot_(slot), generation_(generation) {}

GpuResourceReservation::~GpuResourceReservation() {
    if (pool_) pool_->release(slot_, generation_);
}

}  // namespace mkvc::gpu
