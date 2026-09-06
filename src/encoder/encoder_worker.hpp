#pragma once

#include "encoder_session_state.hpp"

namespace mkvc::encoder {

/** @brief Consume queued frames/barriers and finalize the backend on exit. */
void run_encoder_worker(EncoderSession::Impl* impl) noexcept;

}  // namespace mkvc::encoder
