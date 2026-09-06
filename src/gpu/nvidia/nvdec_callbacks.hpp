#pragma once

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/dynlink_cuviddec.h>
#include <ffnvcodec/dynlink_nvcuvid.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include "mkvcodec/mkvc.h"

namespace mkvc {

struct DecodedFrame;

namespace gpu {

class GpuFrameCore;
class GpuFramePool;

namespace nvidia {

class NvdecRuntimeOwner;

/**
 * @brief State shared by NVDEC sequence, decode and display callbacks.
 *
 * The object normalizes callback diagnostics and owns the mutually exclusive CPU
 * and GPU output queues. GPU display callbacks transfer mapped-frame lifetime to
 * `NvdecRuntimeOwner`; CPU callbacks consume and unmap before returning.
 */
class NvdecCallbackState final {
   public:
    /** Create callback state with a bounded GPU output pool. */
    explicit NvdecCallbackState(size_t gpu_pool_capacity);
    ~NvdecCallbackState();
    NvdecCallbackState(const NvdecCallbackState&) = delete;
    NvdecCallbackState& operator=(const NvdecCallbackState&) = delete;

    /** Attach the parser runtime before the first packet is submitted. */
    void attach_runtime(std::shared_ptr<NvdecRuntimeOwner> runtime);

    /** Select CPU output, rejecting a prior GPU selection. */
    mkvc_result select_cpu(std::string& error);

    /** Select GPU output and enforce bounded pool capacity. */
    mkvc_result select_gpu(std::string& error);

    /** Return whether the selected output queue contains a completed frame. */
    bool output_ready(bool gpu_output) const noexcept;

    /** Move the oldest CPU frame from the callback queue. */
    std::unique_ptr<DecodedFrame> pop_cpu();

    /** Move the oldest GPU frame from the callback queue. */
    std::shared_ptr<GpuFrameCore> pop_gpu();

    /** Release queued frames before closing the parser runtime. */
    void clear_outputs();

    /** Borrow the attached runtime for parser pumping and close. */
    const std::shared_ptr<NvdecRuntimeOwner>& runtime() const noexcept { return runtime_; }

    /** Clear the diagnostic before one synchronous parser call. */
    void clear_error() { callback_error_.clear(); }

    /** Borrow the most recent callback diagnostic. */
    const std::string& error() const noexcept { return callback_error_; }

    /** Build the no-display diagnostic used after parser drain. */
    std::string no_display_diagnostic(uint32_t packets_submitted) const;

    /** Return whether any display callback has run. */
    bool displayed_any() const noexcept { return display_callbacks_ != 0; }

    /** Driver callback implementations; use the free C trampolines below. */
    int sequence(CUVIDEOFORMAT* format);
    int decode(CUVIDPICPARAMS* picture);
    int display(CUVIDPARSERDISPINFO* display);

   private:
    enum class OutputMode { kUnset, kCpu, kGpu };

    std::shared_ptr<NvdecRuntimeOwner> runtime_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::string callback_error_;
    uint32_t sequence_callbacks_ = 0;
    uint32_t decode_callbacks_ = 0;
    uint32_t display_callbacks_ = 0;
    std::deque<std::unique_ptr<DecodedFrame>> completed_cpu_;
    std::deque<std::shared_ptr<GpuFrameCore>> completed_gpu_;
    std::shared_ptr<GpuFramePool> gpu_pool_;
    OutputMode output_mode_ = OutputMode::kUnset;
};

/** C-compatible trampoline for `NvdecCallbackState::sequence`. */
int CUDAAPI nvdec_sequence_callback(void* opaque, CUVIDEOFORMAT* format);

/** C-compatible trampoline for `NvdecCallbackState::decode`. */
int CUDAAPI nvdec_decode_callback(void* opaque, CUVIDPICPARAMS* picture);

/** C-compatible trampoline for `NvdecCallbackState::display`. */
int CUDAAPI nvdec_display_callback(void* opaque, CUVIDPARSERDISPINFO* display);

}  // namespace nvidia
}  // namespace gpu
}  // namespace mkvc
