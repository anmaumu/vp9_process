#include "gpu/intel/vpl_decoder_gpu_output.hpp"

#include <utility>

#include "gpu/gpu_frame.hpp"
#include "gpu/gpu_frame_pool.hpp"
#include "gpu/intel/intel_surface_factory.hpp"
#include "gpu/intel/vpl_session.hpp"

namespace mkvc::gpu::intel {

VplDecoderGpuOutput::VplDecoderGpuOutput(mfxSession session, std::shared_ptr<VplSession> lifetime,
                                         std::shared_ptr<GpuFramePool> pool)
    : session_(session), lifetime_(std::move(lifetime)), pool_(std::move(pool)) {}

bool VplDecoderGpuOutput::has_capacity() const noexcept {
    return pool_ && pool_->in_use() < pool_->capacity();
}

mkvc_result VplDecoderGpuOutput::wrap(mfxFrameSurface1* surface, mfxSyncPoint sync,
                                      std::vector<std::shared_ptr<GpuFrameCore>>& frames,
                                      std::string& error) {
    if (!has_capacity()) {
        error = "Intel GPU frame pool is full";
        return MKVC_WOULD_BLOCK;
    }
    GpuFramePool::Acquisition acquisition;
    const mkvc_result result =
        wrap_vpl_surface(session_, surface, sync, 0, lifetime_, pool_, acquisition, error);
    if (result == MKVC_OK) frames.push_back(std::move(acquisition.core));
    return result;
}

void VplDecoderGpuOutput::close() noexcept {
    pool_.reset();
    lifetime_.reset();
    session_ = nullptr;
}

}  // namespace mkvc::gpu::intel
