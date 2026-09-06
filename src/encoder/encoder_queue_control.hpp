/**
 * @file encoder_queue_control.hpp
 * @brief Producer-side control for the bounded asynchronous encoder queue.
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

/** Insert an ordered flush barrier and wait until the worker completes it. */
mkvc_result flush_and_wait(EncoderSession::Impl& impl, std::string& error);

/** Cancel queued work, complete borrowed submissions, and wake all waiters. */
mkvc_result cancel(EncoderSession::Impl& impl, std::string& error);

/** Request orderly worker close, join it, and publish any terminal error. */
mkvc_result close_and_join(EncoderSession::Impl& impl, std::string& error);

}  // namespace mkvc::encoder
