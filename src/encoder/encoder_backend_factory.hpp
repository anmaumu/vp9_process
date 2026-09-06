/**
 * @file encoder_backend_factory.hpp
 * @brief Type-erased encoder backend selection.
 */
#pragma once

#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc {

class EncoderBackend;

namespace encoder {

/**
 * @brief Create the concrete CPU, Intel, or NVIDIA codec/container backend.
 * @param config Validated encoder configuration.
 * @param error Receives the concrete backend diagnostic on failure.
 * @return Type-erased backend, or nullptr when creation fails.
 */
std::unique_ptr<EncoderBackend> create_backend(const mkvc_encoder_config& config,
                                               std::string& error);

}  // namespace encoder
}  // namespace mkvc
