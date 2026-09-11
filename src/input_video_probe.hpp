/**
 * @file input_video_probe.hpp
 * @brief Backend-neutral Matroska/WebM video-track metadata probing.
 */
#pragma once

#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc {

/** Inspect the first supported VP9/AV1 video track without decoding pixels. */
mkvc_result probe_input_video(const char* path, mkvc_video_info& info, std::string& error);

}  // namespace mkvc
