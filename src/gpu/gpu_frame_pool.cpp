#include "gpu_frame_pool.hpp"

#include <algorithm>
#include <utility>

namespace mkvc::gpu {

GpuFramePool::GpuFramePool(size_t capacity) : slots_(capacity) {}

mkvc_result GpuFramePool::acquire(
    mkvc_gpu_frame_desc desc, std::shared_ptr<Completion> producer,
    std::optional<mkvc_gpu_native_handle_desc> native,
    ResourceRecycle resource_recycle,
    Acquisition& output, std::string& error, BackendResource resource) {
    output = {};
    if (!producer || slots_.empty()) {
        error = "GPU frame pool or producer completion is invalid";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    size_t index = slots_.size();
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t candidate = 0; candidate < slots_.size(); ++candidate) {
            if (!slots_[candidate].in_use) { index = candidate; break; }
        }
        if (index == slots_.size()) {
            error = "GPU frame pool is full";
            return MKVC_WOULD_BLOCK;
        }
        auto& slot = slots_[index];
        slot.in_use = true;
        generation = ++slot.generation;
        ++in_use_;
        peak_in_use_ = std::max(peak_in_use_, in_use_);
    }
    desc.generation = generation;
    if (native) native->generation = generation;
    std::weak_ptr<GpuFramePool> weak = weak_from_this();
    try {
        output.core = std::make_shared<GpuFrameCore>(
            desc, std::move(producer),
            [weak, index, release = std::move(resource_recycle)](
                uint64_t completed_generation) {
                if (release) release();
                if (auto pool = weak.lock()) pool->recycle(index, completed_generation);
            }, std::move(native), resource);
        output.slot = index;
        output.generation = generation;
        return MKVC_OK;
    } catch (...) {
        recycle(index, generation);
        throw;
    }
}

void GpuFramePool::recycle(size_t slot, uint64_t generation) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot >= slots_.size()) return;
    auto& candidate = slots_[slot];
    if (!candidate.in_use || candidate.generation != generation) return;
    candidate.in_use = false;
    if (in_use_ != 0) --in_use_;
}

size_t GpuFramePool::capacity() const noexcept { return slots_.size(); }
size_t GpuFramePool::in_use() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_); return in_use_;
}
size_t GpuFramePool::peak_in_use() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_); return peak_in_use_;
}

}  // namespace mkvc::gpu
