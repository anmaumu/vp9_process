#pragma once

#include "cpu_vp9_encoder.hpp"
#include "mkvcodec/mkvc.h"

#include <memory>
#include <string>

namespace mkvc {

namespace gpu { class GpuFrameCore; }

/**
 * @brief Owns synchronous or bounded asynchronous encoder execution.
 *
 * In asynchronous mode all caller memory is copied before submission returns,
 * and only the worker thread calls the codec/container implementation.
 */
class EncoderSession {
 public:
    struct Impl;

    static std::unique_ptr<EncoderSession> create(
        const mkvc_encoder_config& config, std::string& error);
    ~EncoderSession();

    EncoderSession(const EncoderSession&) = delete;
    EncoderSession& operator=(const EncoderSession&) = delete;

    /** Submit a frame, optionally blocking for bounded queue capacity. */
    mkvc_result write(const mkvc_frame_view& frame, bool block,
                      std::string& error);
    /** Synchronously submit a GPU-resident frame to a compatible backend. */
    mkvc_result write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                          std::string& error);
    /** Insert and wait for an ordered codec flush barrier. */
    mkvc_result flush(std::string& error);
    /** Drain queued frames, finalize output, and join the worker. */
    mkvc_result close(std::string& error);
    /** Snapshot cumulative queue/backend observations. */
    void get_metrics(mkvc_pipeline_metrics& metrics) const;

 private:
    explicit EncoderSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace mkvc
