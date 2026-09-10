#include "cpu_av1_encoder_runtime.hpp"

#include "cpu_av1_encoder_state.hpp"

#if defined(MKVC_HAS_CPU_AV1)

namespace mkvc::cpu_av1_detail {
namespace {

mkvc_result collect_packets(CpuAv1Encoder::Impl& impl, bool drain, std::string& error) {
    while (true) {
        EbBufferHeaderType* packet = nullptr;
        const EbErrorType status = svt_av1_enc_get_packet(impl.codec, &packet, drain ? 1 : 0);
        if (status == EB_NoErrorEmptyQueue) return MKVC_OK;
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
        if (eos) return MKVC_OK;
    }
}

}  // namespace

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

mkvc_result encode_frame(CpuAv1Encoder::Impl& impl, int64_t requested_pts, std::string& error) {
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
    header.pts = impl.timing.select(requested_pts);
    if (svt_av1_enc_send_picture(impl.codec, &header) != EB_ErrorNone) {
        error = "SVT-AV1 rejected an input frame";
        return MKVC_ERROR_CODEC;
    }
    impl.timing.commit(header.pts);
    ++impl.frames_in_sequence;
    return collect_packets(impl, false, error);
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

}  // namespace mkvc::cpu_av1_detail

#endif
