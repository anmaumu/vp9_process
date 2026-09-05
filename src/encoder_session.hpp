#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "cpu_vp9_encoder.hpp"
#include "mkvcodec/mkvc.h"

namespace mkvc {

namespace gpu {
class GpuFrameCore;
}

/**
 * @brief Thread-safe completion and ownership state for borrowed CPU input.
 *
 * The submission retains its input owner until completion, cancellation, or
 * failure becomes terminal. Query is nonblocking; wait supports finite and
 * infinite timeouts.
 */
class CpuSubmission {
   public:
    /** @brief Retain caller memory until the submission becomes terminal. */
    void set_owner(std::shared_ptr<void> owner) noexcept;
    /** @brief Publish exactly one terminal result and release the owner. */
    void complete(mkvc_result result, std::string error) noexcept;
    /** @brief Read pending/complete/cancelled/failed state without blocking. */
    mkvc_result query(uint32_t& status, std::string& error) const;
    /** @brief Wait up to timeout_ms for the terminal result. */
    mkvc_result wait(uint32_t timeout_ms, std::string& error) const;

   private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    bool terminal_ = false;
    mkvc_result result_ = MKVC_OK;
    std::string error_;
    std::shared_ptr<void> owner_;
};

/**
 * @brief Owns synchronous or bounded asynchronous encoder execution.
 *
 * In asynchronous mode all caller memory is copied before submission returns,
 * and only the worker thread calls the codec/container implementation.
 */
class EncoderSession {
   public:
    struct Impl;

    /**
     * @brief Create one type-erased CPU, Intel, or NVIDIA encoder session.
     * @param config Validated stable-ABI encoder configuration.
     * @param error Receives a diagnostic on failure.
     * @return Owning session, or nullptr on backend creation failure.
     */
    static std::unique_ptr<EncoderSession> create(const mkvc_encoder_config& config,
                                                  std::string& error);
    ~EncoderSession();

    EncoderSession(const EncoderSession&) = delete;
    EncoderSession& operator=(const EncoderSession&) = delete;

    /** Submit a frame, optionally blocking for bounded queue capacity. */
    mkvc_result write(const mkvc_frame_view& frame, bool block, std::string& error);
    /** Borrow caller memory synchronously; initially requires queue_size=0. */
    mkvc_result write_borrowed(const mkvc_frame_view& frame, std::string& error);
    /** Queue borrowed caller memory and return its completion lease. */
    mkvc_result submit_borrowed(const mkvc_frame_view& frame,
                                std::shared_ptr<CpuSubmission>& submission, std::string& error);
    /**
     * @brief Synchronously submit a GPU-resident frame to a compatible backend.
     * @details CPU backends reject this call without staging the frame.
     */
    mkvc_result write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame, std::string& error);
    /** Configure copy/fallback behavior before any frame is accepted. */
    mkvc_result set_copy_policy(const mkvc_copy_policy& policy, std::string& error);
    /** Insert and wait for an ordered codec flush barrier. */
    mkvc_result flush(std::string& error);
    /** Discard queued work and wake blocked producers/submissions. */
    mkvc_result cancel(std::string& error);
    /** Drain queued frames, finalize output, and join the worker. */
    mkvc_result close(std::string& error);
    /** Snapshot cumulative queue/backend observations. */
    void get_metrics(mkvc_pipeline_metrics& metrics) const;

   private:
    explicit EncoderSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
