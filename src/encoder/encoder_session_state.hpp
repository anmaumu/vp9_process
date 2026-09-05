#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "../encoder_session.hpp"
#include "cpu_frame_copy.hpp"
#include "encoder_backend.hpp"

namespace mkvc {

/** @brief Shared mutable state owned by one EncoderSession instance. */
struct EncoderSession::Impl {
    /** @brief Work item kind consumed in FIFO order by the encoder worker. */
    enum class ItemType { kFrame, kFlush };

    /** @brief One owned/borrowed frame or an ordered flush barrier. */
    struct Item {
        ItemType type = ItemType::kFrame;
        std::unique_ptr<encoder::OwnedFrame> frame;
        mkvc_frame_view borrowed{};
        std::shared_ptr<CpuSubmission> submission;
        uint64_t flush_token = 0;
    };

    std::unique_ptr<EncoderBackend> backend;
    uint32_t width = 0;
    uint32_t height = 0;
    size_t capacity = 0;
    mutable std::mutex mutex;
    std::condition_variable has_items;
    std::condition_variable has_space;
    std::condition_variable state_changed;
    std::deque<Item> queue;
    std::deque<std::unique_ptr<encoder::OwnedFrame>> free_frames;
    std::thread worker;
    bool accepting = true;
    bool close_requested = false;
    bool closed = false;
    bool failed = false;
    bool canceled = false;
    mkvc_result terminal_result = MKVC_OK;
    std::string terminal_error;
    uint64_t next_flush_token = 0;
    uint64_t completed_flush_token = 0;
    uint64_t accepted_frames = 0;
    uint64_t completed_frames = 0;
    uint64_t rejected_frames = 0;
    uint64_t queue_wait_ns = 0;
    uint64_t backend_time_ns = 0;
    uint32_t peak_queue_depth = 0;
    uint32_t hardware_pending_peak = 0;
    uint32_t copy_path = MKVC_COPY_PATH_UNKNOWN;
    bool require_gpu_resident = false;
    bool allow_gpu_copy = true;
    bool allow_cpu_copy = true;
#if defined(MKVC_ENABLE_TEST_HOOKS)
    uint64_t test_fail_after = std::numeric_limits<uint64_t>::max();
    uint32_t test_delay_ms = 0;
#endif
};

}  // namespace mkvc
