/**
 * @file frame_conversion.hpp
 * @brief Internal CPU conversion from decoded I420 storage.
 */
#pragma once

#include <string>

#include "cpu_vp9_decoder.hpp"
#include "mkvcodec/mkvc.h"

namespace mkvc {

/**
 * @brief Copy or convert an owned decoded I420 frame into a caller-provided view.
 * @param source Owned decoded I420 image.
 * @param destination Validated caller-owned output planes and format.
 * @param error Receives validation or conversion diagnostics.
 * @return MKVC_OK or a stable validation/conversion error.
 */
mkvc_result copy_frame_to(const DecodedFrame& source, mkvc_mutable_frame_view& destination,
                          std::string& error, uint32_t conversion_threads = 0);

}  // namespace mkvc
