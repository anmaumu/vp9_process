/**
 * @file c_api_internal.hpp
 * @brief Shared opaque-handle state for the C ABI translation units.
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "cpu_av1_decoder.hpp"
#include "cpu_frame_pool.hpp"
#include "cpu_vp9_decoder.hpp"
#include "encoder_session.hpp"
#include "intel_webm_decoder.hpp"
#include "mkvcodec/mkvc.h"
#include "nvidia_webm_decoder.hpp"

/** @brief Opaque C encoder handle owning one C++ encoder session. */
struct mkvc_encoder {
    std::unique_ptr<mkvc::EncoderSession> implementation;
};

/** @brief Opaque C decoder handle including bounded prefetch state. */
struct mkvc_decoder {
    std::unique_ptr<mkvc::CpuVp9Decoder> implementation;
    std::unique_ptr<mkvc::CpuAv1Decoder> av1_implementation;
    std::unique_ptr<mkvc::IntelWebmDecoder> intel_implementation;
    std::unique_ptr<mkvc::NvidiaWebmDecoder> nvidia_implementation;
    mutable std::mutex mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;
    std::deque<std::unique_ptr<mkvc::DecodedFrame>> queue;
    std::thread worker;
    size_t capacity = 0;
    bool stop_requested = false;
    bool worker_finished = false;
    mkvc_result worker_result = MKVC_OK;
    std::string worker_error;
    uint64_t accepted_frames = 0;
    uint64_t completed_frames = 0;
    uint64_t queue_wait_ns = 0;
    uint64_t backend_time_ns = 0;
    uint32_t peak_queue_depth = 0;
    uint32_t hardware_pending_peak = 0;
    bool gpu_path_exercised = false;
    bool require_gpu_resident = false;
    bool allow_gpu_copy = true;
    bool allow_cpu_copy = true;
};

/** @brief Reference-counted decoded CPU frame exposed through the C ABI. */
struct mkvc_frame {
    std::atomic<uint32_t> references{1};
    std::unique_ptr<mkvc::DecodedFrame> implementation;
};

/** @brief Opaque asynchronous CPU submission completion handle. */
struct mkvc_submission {
    std::shared_ptr<mkvc::CpuSubmission> implementation;
};

/** @brief Opaque fixed-capacity native CPU frame pool handle. */
struct mkvc_cpu_frame_pool {
    std::shared_ptr<mkvc::CpuFramePool> implementation;
};

/** @brief Opaque generation-checked CPU pool slot lease. */
struct mkvc_cpu_buffer {
    std::shared_ptr<mkvc::CpuBufferLease> implementation;
};

/** @brief Per-thread diagnostic text returned by mkvc_get_last_error(). */
extern thread_local std::string mkvc_last_error;

namespace mkvc::capi {

/**
 * @brief Store a diagnostic for the calling thread and return its result code.
 * @param result Stable C ABI result code.
 * @param message Human-readable diagnostic owned by this call.
 * @return The unchanged result code.
 */
inline mkvc_result fail(mkvc_result result, std::string message) {
    mkvc_last_error = std::move(message);
    return result;
}

}  // namespace mkvc::capi
