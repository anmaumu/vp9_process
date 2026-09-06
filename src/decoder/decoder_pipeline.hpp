/**
 * @file decoder_pipeline.hpp
 * @brief Backend dispatch and bounded prefetch for the C decoder handle.
 */
#pragma once

#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

struct mkvc_decoder;

namespace mkvc {

struct DecodedFrame;

namespace decoder {

/** Create exactly one backend implementation selected by the public config. */
mkvc_result create_backend(mkvc_decoder& decoder, const mkvc_decoder_config& config,
                           std::string& error);

/** Start the bounded CPU prefetch worker when capacity is nonzero. */
void start_prefetch(mkvc_decoder& decoder);

/** Stop and join the prefetch worker, then discard queued CPU frames. */
void stop_prefetch(mkvc_decoder& decoder);

/** Read one CPU frame through either direct or prefetched execution. */
mkvc_result read_cpu(mkvc_decoder& decoder, std::unique_ptr<DecodedFrame>& frame,
                     std::string& error);

/** Read one hardware-resident frame directly and update decoder metrics. */
mkvc_result read_gpu(mkvc_decoder& decoder, mkvc_gpu_frame** frame, std::string& error);

/** Stop prefetch and close the selected backend idempotently. */
mkvc_result close(mkvc_decoder& decoder, std::string& error);

}  // namespace decoder
}  // namespace mkvc
