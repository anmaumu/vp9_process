#pragma once

#include "cpu_vp9_decoder.hpp"
#include "mkvcodec/mkvc.h"

#include <memory>
#include <string>

namespace mkvc {

/** Execute the CPU implementation of an immutable frame-processing plan. */
mkvc_result process_frame_cpu(const DecodedFrame& source,
                              const mkvc_frame_process_config& config,
                              std::unique_ptr<DecodedFrame>& output,
                              std::string& error);

}  // namespace mkvc
