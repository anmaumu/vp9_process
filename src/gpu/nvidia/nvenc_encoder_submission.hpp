#pragma once

#include <memory>
#include <string>

#include "nvidia_webm_encoder.hpp"

namespace mkvc::gpu {
class GpuFrameCore;

namespace nvidia {

/** Convert and submit one CPU frame, then mux the encoded packet. */
mkvc_result write_nvenc_cpu_frame(NvidiaWebmEncoder::Impl& state, const mkvc_frame_view& frame,
                                  std::string& error);

/** Submit one CUDA-resident frame, then mux the encoded packet. */
mkvc_result write_nvenc_gpu_frame(NvidiaWebmEncoder::Impl& state,
                                  const std::shared_ptr<GpuFrameCore>& frame, std::string& error);

}  // namespace nvidia
}  // namespace mkvc::gpu
