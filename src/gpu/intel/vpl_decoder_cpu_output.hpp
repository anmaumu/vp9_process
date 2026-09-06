/**
 * @file vpl_decoder_cpu_output.hpp
 * @brief oneVPL decoded-surface CPU readback and format conversion.
 */
#pragma once

#include <vpl/mfxvideo.h>

#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc {
struct DecodedFrame;
namespace gpu::intel {

/**
 * @brief Map one synchronized NV12 surface and copy it into owned I420 storage.
 * @param surface Borrowed decoded surface; the caller remains responsible for Release.
 * @param frame Receives the converted frame on success.
 * @param error Receives a diagnostic on failure.
 * @return MKVC_OK or a format, mapping, or conversion error.
 */
mkvc_result copy_vpl_surface_to_i420(mfxFrameSurface1* surface,
                                     std::unique_ptr<DecodedFrame>& frame, std::string& error);

}  // namespace gpu::intel
}  // namespace mkvc
