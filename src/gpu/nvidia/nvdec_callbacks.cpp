#include "nvdec_callbacks.hpp"

#include <utility>

#include "cpu_vp9_decoder.hpp"
#include "gpu/gpu_frame.hpp"
#include "gpu/gpu_frame_pool.hpp"
#include "nvdec_api.hpp"
#include "nvdec_cpu_output.hpp"
#include "nvdec_gpu_output.hpp"
#include "nvdec_runtime_owner.hpp"
#include "nvdec_sequence.hpp"

namespace mkvc::gpu::nvidia {

NvdecCallbackState::NvdecCallbackState(size_t gpu_pool_capacity)
    : gpu_pool_(std::make_shared<GpuFramePool>(gpu_pool_capacity)) {}

NvdecCallbackState::~NvdecCallbackState() = default;

void NvdecCallbackState::attach_runtime(std::shared_ptr<NvdecRuntimeOwner> runtime) {
    runtime_ = std::move(runtime);
}

mkvc_result NvdecCallbackState::select_cpu(std::string& error) {
    if (output_mode_ == OutputMode::kGpu) {
        error = "NVIDIA decoder output mode cannot switch from GPU to CPU";
        return MKVC_ERROR_INVALID_STATE;
    }
    output_mode_ = OutputMode::kCpu;
    return MKVC_OK;
}

mkvc_result NvdecCallbackState::select_gpu(std::string& error) {
    if (output_mode_ == OutputMode::kCpu) {
        error = "NVIDIA decoder output mode cannot switch from CPU to GPU";
        return MKVC_ERROR_INVALID_STATE;
    }
    output_mode_ = OutputMode::kGpu;
    if (gpu_pool_->in_use() >= gpu_pool_->capacity()) {
        error = "NVIDIA GPU frame pool is full";
        return MKVC_WOULD_BLOCK;
    }
    return MKVC_OK;
}

bool NvdecCallbackState::output_ready(bool gpu_output) const noexcept {
    return gpu_output ? !completed_gpu_.empty() : !completed_cpu_.empty();
}

std::unique_ptr<DecodedFrame> NvdecCallbackState::pop_cpu() {
    if (completed_cpu_.empty()) return nullptr;
    auto frame = std::move(completed_cpu_.front());
    completed_cpu_.pop_front();
    return frame;
}

std::shared_ptr<GpuFrameCore> NvdecCallbackState::pop_gpu() {
    if (completed_gpu_.empty()) return nullptr;
    auto frame = std::move(completed_gpu_.front());
    completed_gpu_.pop_front();
    return frame;
}

void NvdecCallbackState::clear_outputs() {
    completed_cpu_.clear();
    completed_gpu_.clear();
}

std::string NvdecCallbackState::no_display_diagnostic(uint32_t packets_submitted) const {
    return "NVDEC parser produced callbacks sequence=" + std::to_string(sequence_callbacks_) +
           " decode=" + std::to_string(decode_callbacks_) +
           " display=0 packets=" + std::to_string(packets_submitted);
}

int NvdecCallbackState::sequence(CUVIDEOFORMAT* format) {
    ++sequence_callbacks_;
    if (!runtime_ || format == nullptr) {
        callback_error_ = format == nullptr ? "NVDEC sequence callback received no format"
                                            : "NVDEC sequence callback has no runtime";
        return 0;
    }
    int decode_surfaces = 0;
    if (configure_nvdec_sequence(runtime_->api(), *format, runtime_->decoder(), width_, height_,
                                 decode_surfaces, callback_error_) != MKVC_OK) {
        return 0;
    }
    return decode_surfaces;
}

int NvdecCallbackState::decode(CUVIDPICPARAMS* picture) {
    ++decode_callbacks_;
    if (!runtime_ || runtime_->decoder() == nullptr || picture == nullptr ||
        runtime_->api().decode_picture(runtime_->decoder(), picture) != CUDA_SUCCESS) {
        callback_error_ = "cuvidDecodePicture failed";
        return 0;
    }
    return 1;
}

int NvdecCallbackState::display(CUVIDPARSERDISPINFO* display_info) {
    ++display_callbacks_;
    if (!runtime_ || runtime_->decoder() == nullptr || display_info == nullptr) {
        callback_error_ = "invalid NVDEC display callback";
        return 0;
    }
    CUVIDPROCPARAMS processing{};
    processing.progressive_frame = display_info->progressive_frame;
    processing.top_field_first = display_info->top_field_first;
    processing.unpaired_field = display_info->repeat_first_field < 0;
    unsigned long long device_pointer = 0;
    unsigned int pitch = 0;
    if (runtime_->api().map_frame(runtime_->decoder(), display_info->picture_index, &device_pointer,
                                  &pitch, &processing) != CUDA_SUCCESS) {
        callback_error_ = "cuvidMapVideoFrame failed";
        return 0;
    }
    if (output_mode_ == OutputMode::kGpu) {
        auto runtime = runtime_;
        std::shared_ptr<GpuFrameCore> frame;
        const mkvc_result acquired = acquire_nvdec_gpu_frame(
            runtime->api(), runtime->decoder(), runtime->context(), device_pointer, pitch, width_,
            height_, display_info->timestamp, gpu_pool_,
            [runtime, device_pointer] { runtime->release_mapping(device_pointer); }, frame,
            callback_error_);
        if (acquired != MKVC_OK) return 0;
        runtime->acquire_mapping();
        completed_gpu_.push_back(std::move(frame));
        return 1;
    }
    std::unique_ptr<DecodedFrame> frame;
    if (consume_nvdec_cpu_frame(runtime_->api(), runtime_->decoder(), device_pointer, pitch, width_,
                                height_, display_info->timestamp, frame,
                                callback_error_) != MKVC_OK) {
        return 0;
    }
    completed_cpu_.push_back(std::move(frame));
    return 1;
}

int CUDAAPI nvdec_sequence_callback(void* opaque, CUVIDEOFORMAT* format) {
    return opaque != nullptr ? static_cast<NvdecCallbackState*>(opaque)->sequence(format) : 0;
}

int CUDAAPI nvdec_decode_callback(void* opaque, CUVIDPICPARAMS* picture) {
    return opaque != nullptr ? static_cast<NvdecCallbackState*>(opaque)->decode(picture) : 0;
}

int CUDAAPI nvdec_display_callback(void* opaque, CUVIDPARSERDISPINFO* display) {
    return opaque != nullptr ? static_cast<NvdecCallbackState*>(opaque)->display(display) : 0;
}

}  // namespace mkvc::gpu::nvidia
