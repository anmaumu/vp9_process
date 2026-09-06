#include "intel_vpl_encoder.hpp"

#include "gpu/gpu_frame.hpp"
#if defined(MKVC_HAS_INTEL_ONEVPL)
#include <vpl/mfxvideo.h>

#include "gpu/intel/vpl_cpu_input.hpp"
#include "gpu/intel/vpl_encoder_queue.hpp"
#include "gpu/intel/vpl_encoder_runtime.hpp"
#include "gpu/intel/vpl_imported_surface_tracker.hpp"
#endif

#include <algorithm>
#include <utility>

namespace mkvc {

struct IntelVplEncoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    mfxSession session = nullptr;
    std::unique_ptr<gpu::intel::VplEncoderRuntime> runtime;
    std::unique_ptr<gpu::intel::VplEncoderQueue> queue;
    std::unique_ptr<gpu::intel::VplImportedSurfaceTracker> imported_surfaces;
#endif
    uint32_t codec = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    int64_t next_pts = 0;
    bool drained = false;
    bool closed = false;
};

IntelVplEncoder::IntelVplEncoder() : impl_(std::make_unique<Impl>()) {}
IntelVplEncoder::~IntelVplEncoder() {
    std::string ignored;
    close(ignored);
}

std::unique_ptr<IntelVplEncoder> IntelVplEncoder::create(
    uint32_t codec, uint32_t width, uint32_t height, uint32_t fps_num, uint32_t fps_den,
    uint32_t quality, uint32_t keyframe_interval_frames, std::string& error, uint32_t async_depth,
    const std::shared_ptr<gpu::GpuFrameCore>& external_device_owner) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)codec;
    (void)width;
    (void)height;
    (void)fps_num;
    (void)fps_den;
    (void)quality;
    (void)keyframe_interval_frames;
    (void)async_depth;
    (void)external_device_owner;
    error = "Intel oneVPL backend was not built";
    return nullptr;
#else
    if ((codec != MKVC_CODEC_VP9 && codec != MKVC_CODEC_AV1) || width == 0 || height == 0 ||
        width > 65520u || height > 65520u || (width & 1u) != 0 || (height & 1u) != 0 ||
        fps_num == 0 || fps_den == 0 || quality > 63 || async_depth == 0 || async_depth > 8) {
        error = "invalid oneVPL encoder configuration";
        return nullptr;
    }
    auto encoder = std::unique_ptr<IntelVplEncoder>(new IntelVplEncoder());
    auto& impl = *encoder->impl_;
    impl.codec = codec;
    impl.width = width;
    impl.height = height;
    impl.fps_num = fps_num;
    impl.fps_den = fps_den;
    gpu::intel::VplEncoderRuntimeConfig runtime_config{};
    runtime_config.codec = codec;
    runtime_config.width = width;
    runtime_config.height = height;
    runtime_config.fps_num = fps_num;
    runtime_config.fps_den = fps_den;
    runtime_config.quality = quality;
    runtime_config.keyframe_interval_frames = keyframe_interval_frames;
    runtime_config.async_depth = async_depth;
    size_t bitstream_capacity = 0;
    impl.runtime = gpu::intel::VplEncoderRuntime::create(runtime_config, external_device_owner,
                                                         bitstream_capacity, error);
    if (!impl.runtime) return nullptr;
    impl.session = impl.runtime->session();
    impl.imported_surfaces = std::make_unique<gpu::intel::VplImportedSurfaceTracker>(
        impl.session, impl.runtime->uses_external_device(),
        impl.runtime->external_device_identity());
    impl.queue = std::make_unique<gpu::intel::VplEncoderQueue>(
        impl.session, codec, fps_num, fps_den, bitstream_capacity, async_depth);
    return encoder;
#endif
}

mkvc_result IntelVplEncoder::write_gpu_surface(const std::shared_ptr<gpu::GpuFrameCore>& frame,
                                               int64_t pts,
                                               std::vector<IntelEncodedPacket>& packets,
                                               std::string& error) {
    packets.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)frame;
    (void)pts;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained) {
        error = "oneVPL encoder is closed or drained";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (!frame || frame->desc().backend != MKVC_BACKEND_INTEL ||
        frame->desc().pixel_format != MKVC_PIXEL_FORMAT_NV12 || frame->desc().width != impl.width ||
        frame->desc().height != impl.height) {
        error = "GPU frame is not a compatible Intel NV12 surface";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    mfxFrameSurface1* surface = nullptr;
    bool imported = false;
    mkvc_result result = impl.imported_surfaces->acquire(frame, surface, imported, error);
    if (result != MKVC_OK) return result;
    const int64_t frame_pts = pts >= 0 ? pts : impl.next_pts;
    const mfxU64 original_timestamp = surface->Data.TimeStamp;
    surface->Data.TimeStamp =
        static_cast<mfxU64>(frame_pts) * 90000ULL * impl.fps_den / impl.fps_num;
    auto completion = std::make_shared<gpu::ManualCompletion>();
    result = impl.queue->submit(surface, error, completion, frame);
    // Imported wrappers are private to this encoder. AV1 may read their
    // metadata asynchronously after EncodeFrameAsync returns: restoring the
    // initial (usually zero) timestamp here corrupts every output PTS.
    // Keep submitted metadata until the imported wrapper is retired.
    if (!imported) surface->Data.TimeStamp = original_timestamp;
    if (result != MKVC_OK && result != MKVC_END_OF_STREAM) {
        completion->fail(error);
        return result;
    }
    if (result == MKVC_OK) {
        result = frame->add_consumer(completion, error);
        if (result != MKVC_OK) return result;
    } else {
        completion->complete();
    }
    // A decoder-owned pool can be smaller than the encoder reorder window.
    // Complete one submitted dependency per call so the upstream pool always
    // makes progress. This remains GPU-resident; only the host API waits.
    if (result == MKVC_OK && impl.queue->pending_count() != 0) {
        result = impl.queue->collect_oldest(packets, error);
    }
    if (imported) impl.imported_surfaces->retire();
    if (result == MKVC_OK || result == MKVC_END_OF_STREAM) {
        impl.next_pts = std::max(impl.next_pts, frame_pts + 1);
        return MKVC_OK;
    }
    return result;
#endif
}

mkvc_result IntelVplEncoder::write_nv12(const uint8_t* y, int32_t y_stride, const uint8_t* uv,
                                        int32_t uv_stride, int64_t pts,
                                        std::vector<IntelEncodedPacket>& packets,
                                        std::string& error) {
    packets.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)y;
    (void)y_stride;
    (void)uv;
    (void)uv_stride;
    (void)pts;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained) {
        error = "oneVPL encoder is closed or drained";
        return MKVC_ERROR_INVALID_STATE;
    }
    return gpu::intel::submit_cpu_nv12(impl.session, *impl.queue, impl.width, impl.height,
                                       impl.fps_num, impl.fps_den, impl.next_pts, y, y_stride, uv,
                                       uv_stride, pts, packets, error);
#endif
}

mkvc_result IntelVplEncoder::drain(std::vector<IntelEncodedPacket>& packets, std::string& error) {
    packets.clear();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.drained) return MKVC_OK;
    const mkvc_result result = impl.queue->drain(packets, error);
    if (result != MKVC_OK) return result;
    impl.drained = true;
    return MKVC_OK;
#endif
}

mkvc_result IntelVplEncoder::close(std::string& error) {
    (void)error;
    if (impl_->closed) return MKVC_OK;
#if defined(MKVC_HAS_INTEL_ONEVPL)
    if (impl_->queue) impl_->queue->close();
    // Release our wrapper references while the component still exists. Keep
    // original VA resources alive until runtime teardown drops its references.
    if (impl_->imported_surfaces) impl_->imported_surfaces->release_wrappers_before_runtime_close();
    impl_->runtime.reset();
    impl_->session = nullptr;
    if (impl_->imported_surfaces) impl_->imported_surfaces->release_owners_after_runtime_close();
    impl_->imported_surfaces.reset();
#endif
    impl_->closed = true;
    return MKVC_OK;
}

uint32_t IntelVplEncoder::max_pending_observed() const {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    return impl_->queue ? impl_->queue->max_pending_observed() : 0;
#else
    return 0;
#endif
}

#if defined(MKVC_ENABLE_TEST_HOOKS)
void IntelVplEncoder::set_test_device_loss_after(uint32_t completed_syncpoints) {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    if (impl_->queue) impl_->queue->set_test_device_loss_after(completed_syncpoints);
#else
    (void)completed_syncpoints;
#endif
}
#endif

}  // namespace mkvc
