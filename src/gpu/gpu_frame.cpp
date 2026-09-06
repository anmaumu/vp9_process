#include "gpu_frame.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

extern thread_local std::string mkvc_last_error;

struct mkvc_gpu_frame {
    std::atomic<uint32_t> references{1};
    std::shared_ptr<mkvc::gpu::GpuFrameCore> core;
    uint64_t generation = 0;
};

namespace mkvc::gpu {

mkvc_gpu_completion_status ManualCompletion::query(std::string& error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_ == MKVC_GPU_COMPLETION_FAILED) error = error_;
    return status_;
}

mkvc_result ManualCompletion::wait(uint32_t timeout_ms, std::string& error) const {
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

namespace {

mkvc_result gpu_fail(mkvc_result result, std::string message) {
    mkvc_last_error = std::move(message);
    return result;
}

bool valid(const mkvc_gpu_frame* frame) {
    return frame != nullptr && frame->core != nullptr &&
           frame->generation == frame->core->desc().generation && !frame->core->recycled();
}

}  // namespace

extern "C" {

mkvc_result mkvc_gpu_frame_retain(mkvc_gpu_frame* frame) {
    if (!valid(frame)) return gpu_fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
    frame->references.fetch_add(1, std::memory_order_relaxed);
    return MKVC_OK;
}

void mkvc_gpu_frame_release(mkvc_gpu_frame* frame) {
    if (frame != nullptr && frame->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        frame->core->release_external();
        delete frame;
    }
}

mkvc_result mkvc_gpu_frame_get_desc(const mkvc_gpu_frame* frame, mkvc_gpu_frame_desc* out_desc) {
    if (!valid(frame)) return gpu_fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
    if (out_desc == nullptr || out_desc->struct_size < sizeof(*out_desc) ||
        out_desc->struct_version != 1) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid GPU frame descriptor");
    }
    *out_desc = frame->core->desc();
    return MKVC_OK;
}

mkvc_result mkvc_gpu_frame_query_completion(const mkvc_gpu_frame* frame, uint32_t* out_status) {
    if (!valid(frame)) return gpu_fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
    if (out_status == nullptr)
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, "completion output is null");
    std::string error;
    *out_status = frame->core->producer_completion()->query(error);
    frame->core->poll_recycle();
    return *out_status == MKVC_GPU_COMPLETION_FAILED ? gpu_fail(MKVC_ERROR_CODEC, std::move(error))
                                                     : MKVC_OK;
}

mkvc_result mkvc_gpu_frame_wait(const mkvc_gpu_frame* frame, uint32_t timeout_ms) {
    if (!valid(frame)) return gpu_fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
    std::string error;
    const mkvc_result result = frame->core->producer_completion()->wait(timeout_ms, error);
    frame->core->poll_recycle();
    return result == MKVC_OK ? result : gpu_fail(result, std::move(error));
}

mkvc_result mkvc_gpu_frame_get_native_handle(const mkvc_gpu_frame* frame,
                                             mkvc_gpu_native_handle_desc* out_handle) {
    if (!valid(frame)) return gpu_fail(MKVC_ERROR_INVALID_STATE, "invalid or released GPU frame");
    if (out_handle == nullptr || out_handle->struct_size < sizeof(*out_handle) ||
        out_handle->struct_version != 1) {
        return gpu_fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid native handle output");
    }
    std::string error;
    mkvc_gpu_native_handle_desc value{};
    const mkvc_result result = frame->core->get_native_handle(value, error);
    if (result != MKVC_OK) return gpu_fail(result, std::move(error));
    *out_handle = value;
    return MKVC_OK;
}

}  // extern "C"
