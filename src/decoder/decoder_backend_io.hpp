/**
 * @file decoder_backend_io.hpp
 * @brief Internal access to the decoder implementation selected by the C handle.
 */
#pragma once

#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

struct mkvc_decoder;

namespace mkvc {
struct DecodedFrame;

namespace decoder {

/** Read one CPU frame from the selected concrete backend. */
mkvc_result read_backend(mkvc_decoder& decoder, std::unique_ptr<DecodedFrame>& frame,
                         std::string& error);

/** Close the selected concrete backend. */
mkvc_result close_backend(mkvc_decoder& decoder, std::string& error);

/** Return the backend's observed hardware queue peak, or zero for CPU. */
uint32_t hardware_pending(const mkvc_decoder& decoder);

}  // namespace decoder
}  // namespace mkvc
