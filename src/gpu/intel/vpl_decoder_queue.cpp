#include "vpl_decoder_queue.hpp"

#include <utility>

#include "vpl_decoder_queue_state.hpp"
#include "vpl_session.hpp"

namespace mkvc::gpu::intel {

VplDecoderQueue::VplDecoderQueue(mfxSession session, uint32_t async_depth,
                                 std::shared_ptr<VplSession> lifetime,
                                 std::shared_ptr<GpuFramePool> gpu_pool)
    : impl_(std::make_unique<Impl>()) {
    impl_->session = session;
    impl_->async_depth = async_depth;
    impl_->gpu_output =
        std::make_unique<VplDecoderGpuOutput>(session, std::move(lifetime), std::move(gpu_pool));
}

VplDecoderQueue::~VplDecoderQueue() { close(); }

size_t VplDecoderQueue::pending_count() const noexcept { return impl_->pending.size(); }

uint32_t VplDecoderQueue::async_depth() const noexcept { return impl_->async_depth; }

uint32_t VplDecoderQueue::max_pending_observed() const noexcept {
    return impl_->max_pending.load(std::memory_order_relaxed);
}

#if defined(MKVC_ENABLE_TEST_HOOKS)
void VplDecoderQueue::set_test_device_loss_after(uint32_t completed_syncpoints) noexcept {
    impl_->test_device_loss_after = completed_syncpoints;
}
#endif

}  // namespace mkvc::gpu::intel
