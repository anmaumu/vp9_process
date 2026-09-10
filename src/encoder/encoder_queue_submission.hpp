/**
 * @file encoder_queue_submission.hpp
 * @brief Owned and borrowed producer paths for the asynchronous encoder queue.
 */
#pragma once

#include <memory>
#include <string>

#include "encoder_session_state.hpp"

namespace mkvc::encoder {

/** Deep-copy and enqueue one CPU frame, respecting blocking/backpressure policy. */
mkvc_result enqueue_owned(EncoderSession::Impl& impl, const mkvc_frame_view& frame, bool block,
                          std::string& error);

/** Enqueue one borrowed CPU frame and return its completion ownership state. */
mkvc_result enqueue_borrowed(EncoderSession::Impl& impl, const mkvc_frame_view& frame,
                             std::shared_ptr<CpuSubmission>& submission, std::string& error);

}  // namespace mkvc::encoder
