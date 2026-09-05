#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc {

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

}  // namespace mkvc
