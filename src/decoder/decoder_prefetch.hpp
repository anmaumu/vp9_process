/**
 * @file decoder_prefetch.hpp
 * @brief Bounded worker and consumer path for CPU decoder prefetch.
 */
#pragma once

#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

struct mkvc_decoder;

namespace mkvc {
struct DecodedFrame;

namespace decoder {

/** Start the bounded CPU prefetch worker when capacity is nonzero. */
void start_prefetch(mkvc_decoder& decoder);

/** Stop and join the prefetch worker, then discard queued CPU frames. */
void stop_prefetch(mkvc_decoder& decoder);

/** Consume one CPU frame or terminal result from the prefetch queue. */
mkvc_result read_prefetched(mkvc_decoder& decoder, std::unique_ptr<DecodedFrame>& frame,
                            std::string& error);

}  // namespace decoder
}  // namespace mkvc
