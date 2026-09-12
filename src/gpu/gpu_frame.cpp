/**
 * @file gpu_frame.cpp
 * @brief Backend-neutral GPU completion and frame-lease domain state.
 */
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

#include "gpu_frame_handle.hpp"
#include "pipeline_component_metrics.hpp"

namespace mkvc::gpu {

mkvc_gpu_completion_status ManualCompletion::query(std::string& error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_ == MKVC_GPU_COMPLETION_FAILED) error = error_;
    return status_;
}

mkvc_result ManualCompletion::wait(uint32_t timeout_ms, std::string& error) const {
    ScopedComponentTimer timer(PipelineComponent::kGpuWait);
    std::unique_lock<std::mutex> lock(mutex_);
    const auto done = [this] { return status_ != MKVC_GPU_COMPLETION_PENDING; };
    if (timeout_ms == std::numeric_limits<uint32_t>::max()) {
        changed_.wait(lock, done);
    } else if (!changed_.wait_for(lock, std::chrono::milliseconds(timeout_ms), done)) {
        return MKVC_ERROR_TIMEOUT;
    }
    if (status_ == MKVC_GPU_COMPLETION_FAILED) {
        error = error_;
        return MKVC_ERROR_CODEC;
    }
    return MKVC_OK;
}

void ManualCompletion::complete() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = MKVC_GPU_COMPLETION_COMPLETE;
    }
    changed_.notify_all();
}

void ManualCompletion::fail(std::string error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = std::move(error);
        status_ = MKVC_GPU_COMPLETION_FAILED;
    }
    changed_.notify_all();
}

mkvc_gpu_completion_status CallbackCompletion::query(std::string& error) const {
    bool complete = false;
    const mkvc_result result = query_(complete, error);
    if (result != MKVC_OK) return MKVC_GPU_COMPLETION_FAILED;
    return complete ? MKVC_GPU_COMPLETION_COMPLETE : MKVC_GPU_COMPLETION_PENDING;
}

mkvc_result CallbackCompletion::wait(uint32_t timeout_ms, std::string& error) const {
    ScopedComponentTimer timer(PipelineComponent::kGpuWait);
    const auto started = std::chrono::steady_clock::now();
    while (true) {
        bool complete = false;
        const mkvc_result result = query_(complete, error);
        if (result != MKVC_OK || complete) return result;
        if (timeout_ms != std::numeric_limits<uint32_t>::max() &&
            std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(timeout_ms)) {
            return MKVC_ERROR_TIMEOUT;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

GpuFrameCore::GpuFrameCore(mkvc_gpu_frame_desc desc, std::shared_ptr<Completion> producer,
                           RecycleCallback recycle,
                           std::optional<mkvc_gpu_native_handle_desc> native,
                           BackendResource resource)
    : desc_(desc),
      producer_(std::move(producer)),
      recycle_(std::move(recycle)),
      native_(std::move(native)),
      resource_(resource) {}

GpuFrameCore::~GpuFrameCore() {
    std::vector<std::shared_ptr<Completion>> completions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (recycled_) return;
        if (producer_) completions.push_back(producer_);
        completions.insert(completions.end(), consumers_.begin(), consumers_.end());
    }
    for (const auto& completion : completions) {
        std::string ignored;
        (void)completion->wait(std::numeric_limits<uint32_t>::max(), ignored);
    }
    std::unique_lock<std::mutex> lock(mutex_);
    external_leases_ = 0;
    maybe_recycle_locked(lock);
}

void GpuFrameCore::acquire_external() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (recycled_) throw std::logic_error("GPU frame slot is already recycled");
    ++external_leases_;
}

void GpuFrameCore::release_external() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if (external_leases_ != 0) --external_leases_;
    maybe_recycle_locked(lock);
}

mkvc_result GpuFrameCore::add_consumer(std::shared_ptr<Completion> completion, std::string& error) {
    if (!completion) {
        error = "consumer completion is null";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (recycled_) {
        error = "GPU frame slot is already recycled";
        return MKVC_ERROR_INVALID_STATE;
    }
    consumers_.push_back(std::move(completion));
    return MKVC_OK;
}

bool GpuFrameCore::completions_done_locked() const {
    std::string ignored;
    if (!producer_ || producer_->query(ignored) == MKVC_GPU_COMPLETION_PENDING) return false;
    for (const auto& completion : consumers_) {
        if (completion->query(ignored) == MKVC_GPU_COMPLETION_PENDING) return false;
    }
    return true;
}

void GpuFrameCore::maybe_recycle_locked(std::unique_lock<std::mutex>& lock) noexcept {
    if (recycled_ || external_leases_ != 0 || !completions_done_locked()) return;
    recycled_ = true;
    auto callback = recycle_;
    const uint64_t generation = desc_.generation;
    lock.unlock();
    try {
        if (callback) callback(generation);
    } catch (...) {
    }
}

void GpuFrameCore::poll_recycle() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    maybe_recycle_locked(lock);
}

uint32_t GpuFrameCore::external_leases() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return external_leases_;
}

bool GpuFrameCore::recycled() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return recycled_;
}

mkvc_result GpuFrameCore::get_native_handle(mkvc_gpu_native_handle_desc& output,
                                            std::string& error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (recycled_) {
        error = "GPU frame slot is already recycled";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (!native_) {
        error = "GPU frame has no exportable native handle";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    output = *native_;
    return MKVC_OK;
}

mkvc_gpu_frame* make_handle(const std::shared_ptr<GpuFrameCore>& core) {
    if (!core) return nullptr;
    core->acquire_external();
    try {
        auto handle = std::make_unique<mkvc_gpu_frame>();
        handle->core = core;
        handle->generation = core->desc().generation;
        return handle.release();
    } catch (...) {
        core->release_external();
        throw;
    }
}

std::shared_ptr<GpuFrameCore> get_core(const mkvc_gpu_frame* frame) {
    if (frame == nullptr || frame->core == nullptr ||
        frame->generation != frame->core->desc().generation || frame->core->recycled()) {
        return {};
    }
    return frame->core;
}

}  // namespace mkvc::gpu
