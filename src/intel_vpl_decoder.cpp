#include "intel_vpl_decoder.hpp"

#if defined(MKVC_HAS_INTEL_ONEVPL)
#include "gpu/gpu_frame_pool.hpp"
#include "gpu/intel/vpl_decoder_pump.hpp"
#include "gpu/intel/vpl_decoder_queue.hpp"
#include "gpu/intel/vpl_decoder_runtime.hpp"
#endif

#include <utility>

namespace mkvc {

struct IntelVplDecoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    std::unique_ptr<gpu::intel::VplDecoderRuntime> runtime;
    std::unique_ptr<gpu::intel::VplDecoderQueue> queue;
    std::unique_ptr<gpu::intel::VplDecoderPump> pump;
#endif
    bool gpu_output = false;
    bool drained = false;
    bool closed = false;
};

IntelVplDecoder::IntelVplDecoder() : impl_(std::make_unique<Impl>()) {}
IntelVplDecoder::~IntelVplDecoder() {
    std::string ignored;
    close(ignored);
}

std::unique_ptr<IntelVplDecoder> IntelVplDecoder::create(uint32_t codec, std::string& error,
                                                         uint32_t async_depth, bool gpu_output) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)codec;
    (void)async_depth;
    (void)gpu_output;
    error = "Intel oneVPL backend was not built";
    return nullptr;
#else
    if ((codec != MKVC_CODEC_VP9 && codec != MKVC_CODEC_AV1) || async_depth == 0 ||
        async_depth > 8) {
        error = "invalid oneVPL decoder codec";
        return nullptr;
    }
    auto decoder = std::unique_ptr<IntelVplDecoder>(new IntelVplDecoder());
    auto& impl = *decoder->impl_;
    impl.gpu_output = gpu_output;
    std::shared_ptr<gpu::GpuFramePool> gpu_pool;
    if (gpu_output) gpu_pool = std::make_shared<gpu::GpuFramePool>(async_depth + 2);
    impl.runtime = gpu::intel::VplDecoderRuntime::create(codec, error);
    if (!impl.runtime) return nullptr;
    impl.queue = std::make_unique<gpu::intel::VplDecoderQueue>(
        impl.runtime->session(), async_depth, impl.runtime->lifetime(), std::move(gpu_pool));
    impl.pump = std::make_unique<gpu::intel::VplDecoderPump>(
        codec, gpu_output, impl.runtime->session(), impl.runtime->lifetime(), *impl.queue);
    return decoder;
#endif
}

mkvc_result IntelVplDecoder::decode(const uint8_t* data, size_t size, int64_t pts,
                                    std::vector<std::unique_ptr<DecodedFrame>>& frames,
                                    std::string& error) {
    frames.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)data;
    (void)size;
    (void)pts;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained) {
        error = "oneVPL decoder is closed or drained";
        return MKVC_ERROR_INVALID_STATE;
    }
    return impl.pump->decode_cpu(data, size, pts, frames, error);
#endif
}

mkvc_result IntelVplDecoder::drain(std::vector<std::unique_ptr<DecodedFrame>>& frames,
                                   std::string& error) {
    frames.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained || !impl.pump->initialized()) return MKVC_OK;
    const mkvc_result result = impl.queue->drain_cpu(frames, error);
    if (result != MKVC_OK) return result;
    impl.drained = true;
    return MKVC_OK;
#endif
}

mkvc_result IntelVplDecoder::decode_gpu(const uint8_t* data, size_t size, int64_t pts,
                                        std::vector<std::shared_ptr<gpu::GpuFrameCore>>& frames,
                                        std::string& error) {
    frames.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)data;
    (void)size;
    (void)pts;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (!impl.gpu_output) {
        error = "Intel decoder was not created for GPU output";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (impl.closed || impl.drained) {
        error = "oneVPL decoder is closed or drained";
        return MKVC_ERROR_INVALID_STATE;
    }
    return impl.pump->decode_gpu(data, size, pts, frames, error);
#endif
}

mkvc_result IntelVplDecoder::drain_gpu(std::vector<std::shared_ptr<gpu::GpuFrameCore>>& frames,
                                       std::string& error) {
    frames.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (!impl.gpu_output) {
        error = "Intel decoder was not created for GPU output";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (impl.closed || impl.drained || !impl.pump->initialized()) return MKVC_OK;
    const mkvc_result result = impl.queue->drain_gpu(frames, error);
    if (result != MKVC_OK) return result;
    impl.drained = true;
    return MKVC_OK;
#endif
}

mkvc_result IntelVplDecoder::close(std::string& error) {
    (void)error;
    if (impl_->closed) return MKVC_OK;
#if defined(MKVC_HAS_INTEL_ONEVPL)
    if (impl_->queue) impl_->queue->close();
    impl_->pump.reset();
    impl_->queue.reset();
    impl_->runtime.reset();
#endif
    impl_->closed = true;
    return MKVC_OK;
}

uint32_t IntelVplDecoder::max_pending_observed() const {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    return impl_->queue ? impl_->queue->max_pending_observed() : 0;
#else
    return 0;
#endif
}

#if defined(MKVC_ENABLE_TEST_HOOKS)
void IntelVplDecoder::set_test_device_loss_after(uint32_t completed_syncpoints) {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    if (impl_->queue) impl_->queue->set_test_device_loss_after(completed_syncpoints);
#else
    (void)completed_syncpoints;
#endif
}
#endif

}  // namespace mkvc
