/**
 * @file encoder_backend_execution.hpp
 * @brief Shared backend invocation and metric accounting.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "encoder_session_state.hpp"

namespace mkvc::encoder {

/** Encode one CPU frame through the type-erased backend. */
mkvc_result backend_write(EncoderSession::Impl& impl, const mkvc_frame_view& frame,
                          std::string& error);

/** Flush the type-erased backend. */
mkvc_result backend_flush(EncoderSession::Impl& impl, std::string& error);

/** Close the type-erased backend. */
mkvc_result backend_close(EncoderSession::Impl& impl, std::string& error);

/** Read the backend's current hardware in-flight observation. */
uint32_t backend_hardware_pending(const EncoderSession::Impl& impl);

/** Return nanoseconds elapsed since started. */
uint64_t elapsed_ns(std::chrono::steady_clock::time_point started);

/** Merge one frame path into the session's cumulative copy-path metric. */
void observe_copy_path(EncoderSession::Impl& impl, uint32_t path);

/** Execute and account for one synchronous CPU-frame write. */
mkvc_result write_cpu_sync(EncoderSession::Impl& impl, const mkvc_frame_view& frame,
                           std::string& error);

/** Execute and account for one GPU-frame write while the caller holds impl.mutex. */
mkvc_result write_gpu_sync_locked(EncoderSession::Impl& impl,
                                  const std::shared_ptr<gpu::GpuFrameCore>& frame,
                                  std::string& error);

/** Flush synchronously and account for backend time and hardware depth. */
mkvc_result flush_sync(EncoderSession::Impl& impl, std::string& error);

/** Close synchronously and account for backend time and hardware depth. */
mkvc_result close_sync(EncoderSession::Impl& impl, std::string& error);

}  // namespace mkvc::encoder
