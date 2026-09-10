#include "gpu/intel/vpl_gpu_submission.hpp"

#if defined(MKVC_HAS_INTEL_ONEVPL)

#include <algorithm>
#include <utility>

#include "gpu/gpu_frame.hpp"
#include "intel_vpl_encoder.hpp"
#include "intel_vpl_encoder_state.hpp"

namespace mkvc::gpu::intel {

mkvc_result submit_gpu_surface(IntelVplEncoder::Impl& impl,
                               const std::shared_ptr<GpuFrameCore>& frame, int64_t requested_pts,
                               std::vector<IntelEncodedPacket>& packets, std::string& error) {
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
    const int64_t frame_pts = requested_pts >= 0 ? requested_pts : impl.next_pts;
    const mfxU64 original_timestamp = surface->Data.TimeStamp;
    surface->Data.TimeStamp =
        static_cast<mfxU64>(frame_pts) * 90000ULL * impl.fps_den / impl.fps_num;
    auto completion = std::make_shared<ManualCompletion>();
    result = impl.queue->submit(surface, error, completion, frame);
    // Imported wrappers are private to this encoder. AV1 may read their
    // metadata asynchronously after EncodeFrameAsync returns: restoring the
    // initial timestamp here corrupts every output PTS. Keep submitted
    // metadata until the imported wrapper is retired.
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
    if (result == MKVC_OK && impl.queue->pending_count() != 0)
        result = impl.queue->collect_oldest(packets, error);
    if (imported) impl.imported_surfaces->retire();
    if (result == MKVC_OK || result == MKVC_END_OF_STREAM) {
        impl.next_pts = std::max(impl.next_pts, frame_pts + 1);
        return MKVC_OK;
    }
    return result;
}

}  // namespace mkvc::gpu::intel

#endif
