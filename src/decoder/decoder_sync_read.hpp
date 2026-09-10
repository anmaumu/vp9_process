/**
 * @file decoder_sync_read.hpp
 * @brief Direct CPU and GPU decoder reads without a prefetch worker.
 */
#pragma once

#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

struct mkvc_decoder;

namespace mkvc {
struct DecodedFrame;

namespace decoder {

/** Execute one direct CPU backend read and update timing/count metrics. */
mkvc_result read_cpu_sync(mkvc_decoder& decoder, std::unique_ptr<DecodedFrame>& frame,
                          std::string& error);

/** Execute one direct GPU backend read and update timing/count metrics. */
mkvc_result read_gpu_sync(mkvc_decoder& decoder, mkvc_gpu_frame** frame, std::string& error);

}  // namespace decoder
}  // namespace mkvc
