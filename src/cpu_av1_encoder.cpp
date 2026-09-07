#include "cpu_av1_encoder.hpp"

#include "encoder/cpu_frame_to_i420.hpp"
#include "encoder/frame_timing.hpp"
#include "webm_muxer.hpp"

#if defined(MKVC_HAS_CPU_AV1)
#include <svt-av1/EbSvtAv1Enc.h>
#endif

#include <limits>
#include <string>
#include <vector>

namespace mkvc {

struct CpuAv1Encoder::Impl {
#if defined(MKVC_HAS_CPU_AV1)
    EbComponentType* codec = nullptr;
    bool codec_initialized = false;
    std::unique_ptr<WebmMuxer> muxer;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
    encoder::FrameTiming timing;
    uint32_t quality = 32;
    uint32_t keyframe_interval_frames = 0;
    uint64_t frames_in_sequence = 0;
    bool eos_sent = false;
    bool closed = false;
    std::vector<uint8_t> image;
};

CpuAv1Encoder::CpuAv1Encoder() : impl_(std::make_unique<Impl>()) {}
CpuAv1Encoder::~CpuAv1Encoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_CPU_AV1)
namespace {

mkvc_result collect_packets(CpuAv1Encoder::Impl& impl, bool drain, std::string& error) {
    while (true) {
        EbBufferHeaderType* packet = nullptr;
        const EbErrorType status = svt_av1_enc_get_packet(impl.codec, &packet, drain ? 1 : 0);
        if (status == EB_NoErrorEmptyQueue) {
            return MKVC_OK;
        }
        if (status != EB_ErrorNone || packet == nullptr) {
            error = "SVT-AV1 failed to return an encoded packet";
            return MKVC_ERROR_CODEC;
        }
        const bool eos = (packet->flags & EB_BUFFERFLAG_EOS) != 0;
        if (packet->n_filled_len > 0) {
            const mkvc_result mux_result = impl.muxer->add_frame(
                packet->p_buffer, packet->n_filled_len, impl.timing.to_nanoseconds(packet->pts),
                impl.timing.duration_nanoseconds(),
                packet->pic_type == EB_AV1_KEY_PICTURE ||
                    packet->pic_type == EB_AV1_INTRA_ONLY_PICTURE,
                error);
            if (mux_result != MKVC_OK) {
                svt_av1_enc_release_out_buffer(&packet);
                return mux_result;
            }
        }
        svt_av1_enc_release_out_buffer(&packet);
        if (eos) {
            return MKVC_OK;
        }
        if (!drain) {
            continue;
        }
    }
}

mkvc_result initialize_codec(CpuAv1Encoder::Impl& impl, std::string& error) {
    EbSvtAv1EncConfiguration config{};
    if (svt_av1_enc_init_handle(&impl.codec, &config) != EB_ErrorNone) {
        error = "SVT-AV1 failed to create an encoder handle";
        return MKVC_ERROR_CODEC;
    }
    config.source_width = impl.width;
    config.source_height = impl.height;
    config.frame_rate_numerator = impl.timing.fps_num();
    config.frame_rate_denominator = impl.timing.fps_den();
    config.encoder_bit_depth = 8;
    config.encoder_color_format = EB_YUV420;
    config.level = 63;
    config.rate_control_mode = 0;
    config.qp = impl.quality;
    config.enc_mode = 8;
    config.pred_structure = RANDOM_ACCESS;
    config.intra_period_length =
        impl.keyframe_interval_frames == 0
            ? static_cast<int32_t>(impl.timing.fps_num() * 4 / impl.timing.fps_den()) - 1
            : static_cast<int32_t>(impl.keyframe_interval_frames) - 1;
    if (svt_av1_enc_set_parameter(impl.codec, &config) != EB_ErrorNone ||
        svt_av1_enc_init(impl.codec) != EB_ErrorNone) {
        error = "SVT-AV1 rejected the encoder configuration";
        return MKVC_ERROR_CODEC;
    }
    impl.codec_initialized = true;
    impl.eos_sent = false;
    impl.frames_in_sequence = 0;
    return MKVC_OK;
}

mkvc_result end_sequence(CpuAv1Encoder::Impl& impl, std::string& error) {
    if (!impl.codec_initialized || impl.frames_in_sequence == 0) return MKVC_OK;
    EbBufferHeaderType eos{};
    eos.size = sizeof(eos);
    eos.flags = EB_BUFFERFLAG_EOS;
    if (svt_av1_enc_send_picture(impl.codec, &eos) != EB_ErrorNone) {
        error = "SVT-AV1 rejected end-of-stream";
        return MKVC_ERROR_CODEC;
    }
    impl.eos_sent = true;
    return collect_packets(impl, true, error);
}

void destroy_codec(CpuAv1Encoder::Impl& impl) {
    if (impl.codec_initialized) svt_av1_enc_deinit(impl.codec);
    if (impl.codec != nullptr) svt_av1_enc_deinit_handle(impl.codec);
    impl.codec = nullptr;
    impl.codec_initialized = false;
    impl.eos_sent = false;
    impl.frames_in_sequence = 0;
}

}  // namespace
#endif

std::unique_ptr<CpuAv1Encoder> CpuAv1Encoder::create(const mkvc_encoder_config& config,
                                                     std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    (void)config;
    error = "CPU AV1 backend was not built";
    return nullptr;
#else
    auto encoder = std::unique_ptr<CpuAv1Encoder>(new CpuAv1Encoder());
    auto& impl = *encoder->impl_;
    impl.width = config.width;
    impl.height = config.height;
    impl.timing.configure(config.fps_num, config.fps_den);
    impl.quality = config.quality;
    impl.keyframe_interval_frames = config.keyframe_interval_frames;
    if (initialize_codec(impl, error) != MKVC_OK) return nullptr;
    impl.muxer = WebmMuxer::create(config.output_path_utf8, MKVC_CODEC_AV1, config.width,
                                   config.height, config.fps_num, config.fps_den, error);
    if (!impl.muxer) return nullptr;
    const uint64_t y_size = static_cast<uint64_t>(config.width) * config.height;
    const uint64_t uv_size = static_cast<uint64_t>(config.width / 2) * (config.height / 2);
    if (y_size + 2 * uv_size > std::numeric_limits<size_t>::max()) {
        error = "frame dimensions exceed addressable memory";
        return nullptr;
    }
    impl.image.resize(static_cast<size_t>(y_size + 2 * uv_size));
    return encoder;
#endif
}

mkvc_result CpuAv1Encoder::write(const mkvc_frame_view& frame, std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    (void)frame;
    error = "CPU AV1 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.eos_sent) {
        error = "encoder is closed or draining";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (frame.width != impl.width || frame.height != impl.height) {
        error = "frame dimensions do not match AV1 encoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const mkvc_result conversion = encoder::convert_cpu_frame_to_i420(
        frame, impl.width, impl.height, impl.image, "AV1", error);
    if (conversion != MKVC_OK) return conversion;
    const size_t y_size = static_cast<size_t>(impl.width) * impl.height;
    const size_t uv_size = static_cast<size_t>(impl.width / 2) * (impl.height / 2);
    EbSvtIOFormat input{};
    input.luma = impl.image.data();
    input.cb = input.luma + y_size;
    input.cr = input.cb + uv_size;
    input.y_stride = impl.width;
    input.cb_stride = impl.width / 2;
    input.cr_stride = impl.width / 2;
    EbBufferHeaderType header{};
    header.size = sizeof(header);
    header.p_buffer = reinterpret_cast<uint8_t*>(&input);
    header.n_filled_len = impl.width * impl.height * 3 / 2;
    header.pts = impl.timing.select(frame.pts);
    if (svt_av1_enc_send_picture(impl.codec, &header) != EB_ErrorNone) {
        error = "SVT-AV1 rejected an input frame";
        return MKVC_ERROR_CODEC;
    }
    impl.timing.commit(header.pts);
    ++impl.frames_in_sequence;
    return collect_packets(impl, false, error);
#endif
}

mkvc_result CpuAv1Encoder::flush(std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    error = "CPU AV1 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (impl_->closed) return MKVC_OK;
    if (impl_->frames_in_sequence == 0) return MKVC_OK;
    mkvc_result result = end_sequence(*impl_, error);
    destroy_codec(*impl_);
    if (result != MKVC_OK) return result;
    return initialize_codec(*impl_, error);
#endif
}

mkvc_result CpuAv1Encoder::close(std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    (void)error;
    impl_->closed = true;
    return MKVC_OK;
#else
    auto& impl = *impl_;
    if (impl.closed) return MKVC_OK;
    mkvc_result result = MKVC_OK;
    if (impl.codec_initialized && !impl.eos_sent) result = end_sequence(impl, error);
    if (result == MKVC_OK && impl.muxer) result = impl.muxer->finalize(error);
    destroy_codec(impl);
    impl.closed = true;
    return result;
#endif
}

}  // namespace mkvc
