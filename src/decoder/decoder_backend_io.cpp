#include "decoder/decoder_backend_io.hpp"

#include "c_api_internal.hpp"

namespace mkvc::decoder {

mkvc_result read_backend(mkvc_decoder& decoder, std::unique_ptr<DecodedFrame>& frame,
                         std::string& error) {
    if (decoder.intel_implementation) return decoder.intel_implementation->read(frame, error);
    if (decoder.nvidia_implementation) return decoder.nvidia_implementation->read(frame, error);
    return decoder.implementation ? decoder.implementation->read(frame, error)
                                  : decoder.av1_implementation->read(frame, error);
}

mkvc_result close_backend(mkvc_decoder& decoder, std::string& error) {
    if (decoder.intel_implementation) return decoder.intel_implementation->close(error);
    if (decoder.nvidia_implementation) return decoder.nvidia_implementation->close(error);
    return decoder.implementation ? decoder.implementation->close(error)
                                  : decoder.av1_implementation->close(error);
}

uint32_t hardware_pending(const mkvc_decoder& decoder) {
    if (decoder.intel_implementation) return decoder.intel_implementation->max_pending_observed();
    return decoder.nvidia_implementation ? decoder.nvidia_implementation->max_pending_observed()
                                         : 0;
}

}  // namespace mkvc::decoder
