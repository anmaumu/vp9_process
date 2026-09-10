#include "cpu_vp9_decoder.hpp"

#include "cpu_vp9_decoder_runtime.hpp"
#include "cpu_vp9_decoder_state.hpp"

namespace mkvc {

CpuVp9Decoder::CpuVp9Decoder() : impl_(std::make_unique<Impl>()) {}

CpuVp9Decoder::~CpuVp9Decoder() {
    std::string ignored;
    close(ignored);
}

std::unique_ptr<CpuVp9Decoder> CpuVp9Decoder::create(const mkvc_decoder_config& config,
                                                     std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    (void)config;
    error = "CPU VP9 backend was not built";
    return nullptr;
#else
    auto decoder = std::unique_ptr<CpuVp9Decoder>(new CpuVp9Decoder());
    if (cpu_vp9_decoder_detail::initialize(*decoder->impl_, config, error) != MKVC_OK) {
        return nullptr;
    }
    return decoder;
#endif
}

mkvc_result CpuVp9Decoder::read(std::unique_ptr<DecodedFrame>& frame, std::string& error) {
    frame.reset();
#if !defined(MKVC_HAS_CPU_VP9)
    error = "CPU VP9 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (impl_->closed) {
        error = "decoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    return cpu_vp9_decoder_detail::read_frame(*impl_, frame, error);
#endif
}

mkvc_result CpuVp9Decoder::close(std::string& error) {
    (void)error;
    if (impl_->closed) return MKVC_OK;
#if defined(MKVC_HAS_CPU_VP9)
    cpu_vp9_decoder_detail::destroy(*impl_);
#endif
    impl_->closed = true;
    return MKVC_OK;
}

}  // namespace mkvc
