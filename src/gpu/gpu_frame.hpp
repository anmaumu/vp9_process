#pragma once

#include "mkvcodec/mkvc.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mkvc::gpu {

/** Backend completion adapter implemented by SyncPoint/fence/CUDA event types. */
class Completion {
 public:
    virtual ~Completion() = default;
    virtual mkvc_gpu_completion_status query(std::string& error) const = 0;
    virtual mkvc_result wait(uint32_t timeout_ms, std::string& error) const = 0;
};

/** Deterministic completion used by tests and synchronous backend adapters. */
class ManualCompletion final : public Completion {
 public:
    mkvc_gpu_completion_status query(std::string& error) const override;
    mkvc_result wait(uint32_t timeout_ms, std::string& error) const override;
    void complete();
    void fail(std::string error);

 private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    mkvc_gpu_completion_status status_ = MKVC_GPU_COMPLETION_PENDING;
    std::string error_;
};

/** Polling adapter used to normalize backend event APIs in deterministic tests. */
class CallbackCompletion final : public Completion {
 public:
    using Query = std::function<mkvc_result(bool&, std::string&)>;
    explicit CallbackCompletion(Query query) : query_(std::move(query)) {}
    mkvc_gpu_completion_status query(std::string& error) const override;
    mkvc_result wait(uint32_t timeout_ms, std::string& error) const override;

 private:
    Query query_;
};

/** Shared ownership state behind every C/Python/C# GPU frame lease. */
class GpuFrameCore : public std::enable_shared_from_this<GpuFrameCore> {
 public:
    using RecycleCallback = std::function<void(uint64_t)>;

    GpuFrameCore(mkvc_gpu_frame_desc desc,
                 std::shared_ptr<Completion> producer,
                 RecycleCallback recycle,
                 std::optional<mkvc_gpu_native_handle_desc> native = std::nullopt);
    const mkvc_gpu_frame_desc& desc() const noexcept { return desc_; }
    std::shared_ptr<Completion> producer_completion() const { return producer_; }
    void acquire_external();
    void release_external() noexcept;
    mkvc_result add_consumer(std::shared_ptr<Completion> completion,
                             std::string& error);
    void poll_recycle() noexcept;
    uint32_t external_leases() const noexcept;
    bool recycled() const noexcept;
    mkvc_result get_native_handle(mkvc_gpu_native_handle_desc& output,
                                  std::string& error) const;

 private:
    bool completions_done_locked() const;
    void maybe_recycle_locked(std::unique_lock<std::mutex>& lock) noexcept;

    mkvc_gpu_frame_desc desc_{};
    std::shared_ptr<Completion> producer_;
    RecycleCallback recycle_;
    mutable std::mutex mutex_;
    uint32_t external_leases_ = 0;
    std::vector<std::shared_ptr<Completion>> consumers_;
    bool recycled_ = false;
    std::optional<mkvc_gpu_native_handle_desc> native_;
};

/** Create one opaque ABI lease for a backend-owned core. */
mkvc_gpu_frame* make_handle(const std::shared_ptr<GpuFrameCore>& core);

}  // namespace mkvc::gpu
