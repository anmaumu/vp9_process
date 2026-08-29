#pragma once

#include "cpu_vp9_decoder.hpp"
#include "mkvcodec/mkvc.h"

#include <string>

namespace mkvc {

/** Copy or convert an owned decoded I420 frame into a caller-provided view. */
mkvc_result copy_frame_to(const DecodedFrame& source,
                          mkvc_mutable_frame_view& destination,
                          std::string& error);

}  // namespace mkvc

