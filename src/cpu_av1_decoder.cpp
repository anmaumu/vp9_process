#include "cpu_av1_decoder.hpp"

#include "cpu_av1_decoder_runtime.hpp"
#include "cpu_av1_decoder_state.hpp"

namespace mkvc {

CpuAv1Decoder::CpuAv1Decoder() : impl_(std::make_unique<Impl>()) {}

CpuAv1Decoder::~CpuAv1Decoder() {
    std::string ignored;
    close(ignored);
}

std::unique_ptr<CpuAv1Decoder> CpuAv1Decoder::create(const mkvc_decoder_config& config,
                                                     std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    (void)config;
    error = "CPU AV1 backend was not built";
    return nullptr;
#else
    auto decoder = std::unique_ptr<CpuAv1Decoder>(new CpuAv1Decoder());
    if (cpu_av1_decoder_detail::initialize(*decoder->impl_, config, error) != MKVC_OK) {
        return nullptr;
    }
    return decoder;
#endif
}

mkvc_result CpuAv1Decoder::read(std::unique_ptr<DecodedFrame>& frame, std::string& error) {
    frame.reset();
#if !defined(MKVC_HAS_CPU_AV1)
    error = "CPU AV1 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (impl_->closed) {
        error = "decoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    return cpu_av1_decoder_detail::read_frame(*impl_, frame, error);
#endif
}

mkvc_result CpuAv1Decoder::close(std::string& error) {
    (void)error;
    if (impl_->closed) return MKVC_OK;
#if defined(MKVC_HAS_CPU_AV1)
    cpu_av1_decoder_detail::destroy(*impl_);
#endif
    impl_->closed = true;
    return MKVC_OK;
}

}  // namespace mkvc
