#pragma once

#include <cstdint>
#include <string>

#include "cpu_av1_encoder.hpp"

namespace mkvc {

namespace cpu_av1_detail {

/** Initialize SVT-AV1 from the current encoder state. */
mkvc_result initialize_codec(CpuAv1Encoder::Impl& impl, std::string& error);

/** Submit the converted I420 image and collect currently available packets. */
mkvc_result encode_frame(CpuAv1Encoder::Impl& impl, int64_t requested_pts, std::string& error);

/** Send EOS and drain all remaining SVT-AV1 packets for the sequence. */
mkvc_result end_sequence(CpuAv1Encoder::Impl& impl, std::string& error);

/** Release the SVT-AV1 component and reset sequence-local state. */
void destroy_codec(CpuAv1Encoder::Impl& impl);

}  // namespace cpu_av1_detail
}  // namespace mkvc
