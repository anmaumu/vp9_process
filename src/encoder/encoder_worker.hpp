#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "encoder_session_state.hpp"

namespace mkvc::encoder {

/** @brief Encode one CPU frame through the session's type-erased backend. */
mkvc_result backend_write(EncoderSession::Impl& impl, const mkvc_frame_view& frame,
                          std::string& error);
/** @brief Flush the session's type-erased backend. */
mkvc_result backend_flush(EncoderSession::Impl& impl, std::string& error);
/** @brief Close the session's type-erased backend. */
mkvc_result backend_close(EncoderSession::Impl& impl, std::string& error);
/** @brief Read the current backend hardware in-flight observation. */
uint32_t backend_hardware_pending(const EncoderSession::Impl& impl);
/** @brief Return nanoseconds elapsed since started. */
uint64_t elapsed_ns(std::chrono::steady_clock::time_point started);
/** @brief Merge one frame path into the session's cumulative copy-path metric. */
void observe_copy_path(EncoderSession::Impl& impl, uint32_t path);
/** @brief Consume queued frames/barriers and finalize the backend on exit. */
void run_encoder_worker(EncoderSession::Impl* impl) noexcept;

}  // namespace mkvc::encoder
