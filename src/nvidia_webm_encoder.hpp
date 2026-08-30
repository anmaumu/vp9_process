#pragma once

#include "mkvcodec/mkvc.h"

#include <memory>
#include <string>

namespace mkvc::gpu { class GpuFrameCore; }

namespace mkvc {

/** NVIDIA NVENC AV1 encoder with CPU input conversion and libwebm muxing. */
class NvidiaWebmEncoder {
public:
  struct Impl;
  static std::unique_ptr<NvidiaWebmEncoder>
  create(const mkvc_encoder_config &config, std::string &error);
  ~NvidiaWebmEncoder();
  NvidiaWebmEncoder(const NvidiaWebmEncoder &) = delete;
  NvidiaWebmEncoder &operator=(const NvidiaWebmEncoder &) = delete;
  mkvc_result write(const mkvc_frame_view &frame, std::string &error);
  mkvc_result write_gpu(const std::shared_ptr<gpu::GpuFrameCore> &frame,
                        std::string &error);
  mkvc_result flush(std::string &error);
  mkvc_result close(std::string &error);
  uint32_t max_pending_observed() const;

private:
  NvidiaWebmEncoder();
  std::unique_ptr<Impl> impl_;
};

} // namespace mkvc
