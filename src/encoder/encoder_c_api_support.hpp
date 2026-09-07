/**
 * @file encoder_c_api_support.hpp
 * @brief Encoder-specific C ABI validation and session construction.
 */
#pragma once

#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc {
class EncoderSession;
namespace encoder::capi {

/** Return whether a versioned encoder configuration is safe to read and use. */
bool valid_encoder_config(const mkvc_encoder_config* config) noexcept;

/** Return whether a versioned CPU frame view is safe to pass to EncoderSession. */
bool valid_frame_view(const mkvc_frame_view* frame) noexcept;

/**
 * @brief Check dynamic capability constraints and construct an encoder session.
 * @param config Previously validated encoder configuration.
 * @param session Receives the owning implementation on success.
 * @param error Receives backend selection or codec creation diagnostics.
 * @return MKVC_OK, MKVC_ERROR_NOT_SUPPORTED, or MKVC_ERROR_CODEC.
 */
mkvc_result create_encoder_session(const mkvc_encoder_config& config,
                                   std::unique_ptr<EncoderSession>& session, std::string& error);

}  // namespace encoder::capi
}  // namespace mkvc
