#include "cpu_submission.hpp"

#include <chrono>
#include <limits>
#include <utility>

namespace mkvc {

void CpuSubmission::complete(mkvc_result result, std::string error) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (terminal_) return;
    terminal_ = true;
    result_ = result;
    error_ = std::move(error);
    owner_.reset();
    changed_.notify_all();
}

void CpuSubmission::set_owner(std::shared_ptr<void> owner) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) owner_ = std::move(owner);
}

mkvc_result CpuSubmission::query(uint32_t& status, std::string& error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) {
        status = MKVC_SUBMISSION_PENDING;
        return MKVC_OK;
    }
    status = result_ == MKVC_OK                ? MKVC_SUBMISSION_COMPLETE
             : result_ == MKVC_ERROR_CANCELLED ? MKVC_SUBMISSION_CANCELLED
                                               : MKVC_SUBMISSION_FAILED;
    error = error_;
    return MKVC_OK;
}

mkvc_result CpuSubmission::wait(uint32_t timeout_ms, std::string& error) const {
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout_ms == std::numeric_limits<uint32_t>::max()) {
        changed_.wait(lock, [this] { return terminal_; });
    } else if (!changed_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                  [this] { return terminal_; })) {
        error = "borrowed submission wait timed out";
        return MKVC_ERROR_TIMEOUT;
    }
    error = error_;
    return result_;
}

}  // namespace mkvc
