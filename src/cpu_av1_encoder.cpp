#include "cpu_av1_encoder.hpp"

#if defined(MKVC_HAS_CPU_AV1)
#include <svt-av1/EbSvtAv1Enc.h>
#include <webm/mkvmuxer/mkvmuxer.h>
#include <webm/mkvmuxer/mkvwriter.h>
#endif

#include <algorithm>
#include <string>

namespace mkvc {

struct CpuAv1Encoder::Impl {
#if defined(MKVC_HAS_CPU_AV1)
    EbComponentType* codec = nullptr;
    bool codec_initialized = false;
    mkvmuxer::MkvWriter writer;
    bool writer_open = false;
    mkvmuxer::Segment segment;
    bool segment_initialized = false;
    uint64_t track_number = 0;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    int64_t next_pts = 0;
    bool eos_sent = false;
    bool closed = false;
};

CpuAv1Encoder::CpuAv1Encoder() : impl_(std::make_unique<Impl>()) {}
CpuAv1Encoder::~CpuAv1Encoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_CPU_AV1)
namespace {

mkvc_result collect_packets(CpuAv1Encoder::Impl& impl, bool drain,
                            std::string& error) {
    while (true) {
        EbBufferHeaderType* packet = nullptr;
        const EbErrorType status =
            svt_av1_enc_get_packet(impl.codec, &packet, drain ? 1 : 0);
        if (status == EB_NoErrorEmptyQueue) {
            return MKVC_OK;
        }
        if (status != EB_ErrorNone || packet == nullptr) {
            error = "SVT-AV1 failed to return an encoded packet";
            return MKVC_ERROR_CODEC;
        }
        const bool eos = (packet->flags & EB_BUFFERFLAG_EOS) != 0;
        if (packet->n_filled_len > 0) {
            const uint64_t timestamp_ns =
                static_cast<uint64_t>(packet->pts) * impl.fps_den *
                1000000000ULL / impl.fps_num;
            mkvmuxer::Frame frame;
            const bool initialized = frame.Init(packet->p_buffer,
                                                packet->n_filled_len);
            frame.set_track_number(impl.track_number);
            frame.set_timestamp(timestamp_ns);
            frame.set_duration(static_cast<uint64_t>(impl.fps_den) *
                               1000000000ULL / impl.fps_num);
            frame.set_is_key(packet->pic_type == EB_AV1_KEY_PICTURE ||
                             packet->pic_type == EB_AV1_INTRA_ONLY_PICTURE);
            if (!initialized || !impl.segment.AddGenericFrame(&frame)) {
                svt_av1_enc_release_out_buffer(&packet);
                error = "libwebm failed to mux AV1 packet pts=" +
                        std::to_string(packet->pts) + " type=" +
                        std::to_string(static_cast<int>(packet->pic_type)) +
                        " flags=" + std::to_string(packet->flags);
                return MKVC_ERROR_IO;
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

}  // namespace
#endif

std::unique_ptr<CpuAv1Encoder> CpuAv1Encoder::create(
    const mkvc_encoder_config& config, std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    (void)config;
    error = "CPU AV1 backend was not built";
    return nullptr;
#else
    auto encoder = std::unique_ptr<CpuAv1Encoder>(new CpuAv1Encoder());
    auto& impl = *encoder->impl_;
    impl.width = config.width;
    impl.height = config.height;
    impl.fps_num = config.fps_num;
    impl.fps_den = config.fps_den;
    EbSvtAv1EncConfiguration codec_config{};
    if (svt_av1_enc_init_handle(&impl.codec, &codec_config) != EB_ErrorNone) {
        error = "SVT-AV1 failed to create an encoder handle";
        return nullptr;
    }
    codec_config.source_width = config.width;
    codec_config.source_height = config.height;
    codec_config.frame_rate_numerator = config.fps_num;
    codec_config.frame_rate_denominator = config.fps_den;
    codec_config.encoder_bit_depth = 8;
    codec_config.encoder_color_format = EB_YUV420;
    // Pin the sequence level so the Matroska AV1CodecConfigurationRecord is
    // deterministic. Level 6.3 is legal for every supported 8-bit size here.
    codec_config.level = 63;
    codec_config.rate_control_mode = 0;
    codec_config.qp = config.quality;
    codec_config.enc_mode = 8;
    codec_config.pred_structure = RANDOM_ACCESS;
    codec_config.intra_period_length = config.keyframe_interval_frames == 0
        ? static_cast<int32_t>(config.fps_num * 4 / config.fps_den) - 1
        : static_cast<int32_t>(config.keyframe_interval_frames) - 1;
    if (svt_av1_enc_set_parameter(impl.codec, &codec_config) != EB_ErrorNone ||
        svt_av1_enc_init(impl.codec) != EB_ErrorNone) {
        error = "SVT-AV1 rejected the encoder configuration";
        return nullptr;
    }
    impl.codec_initialized = true;
    if (!impl.writer.Open(config.output_path_utf8)) {
        error = "failed to open output";
        return nullptr;
    }
    impl.writer_open = true;
    if (!impl.segment.Init(&impl.writer)) {
        error = "failed to initialize libwebm";
        return nullptr;
    }
    impl.segment_initialized = true;
    impl.segment.set_mode(mkvmuxer::Segment::kFile);
    impl.track_number = impl.segment.AddVideoTrack(
        static_cast<int32_t>(config.width), static_cast<int32_t>(config.height), 0);
    auto* track = static_cast<mkvmuxer::VideoTrack*>(
        impl.segment.GetTrackByNumber(impl.track_number));
    if (impl.track_number == 0 || track == nullptr) {
        error = "libwebm failed to create the AV1 video track";
        return nullptr;
    }
    track->set_codec_id(mkvmuxer::Tracks::kAv1CodecId);
    const uint8_t av1_codec_configuration[4] = {
        0x81,  // marker=1, version=1
        19,    // profile=0, level index 19 (AV1 level 6.3)
        0x0c,  // 8-bit, 4:2:0, chroma sample position unknown
        0x00,
    };
    if (!track->SetCodecPrivate(av1_codec_configuration,
                                sizeof(av1_codec_configuration))) {
        error = "libwebm rejected the AV1 codec configuration";
        return nullptr;
    }
    track->set_frame_rate(static_cast<double>(config.fps_num) / config.fps_den);
    track->set_default_duration(static_cast<uint64_t>(config.fps_den) *
                                1000000000ULL / config.fps_num);
    return encoder;
#endif
}

mkvc_result CpuAv1Encoder::write(const mkvc_frame_view& frame,
                                 std::string& error) {
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
    if (frame.width != impl.width || frame.height != impl.height ||
        frame.pixel_format != MKVC_PIXEL_FORMAT_I420 ||
        frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
        frame.planes[2] == nullptr ||
        frame.strides[0] < static_cast<int32_t>(impl.width) ||
        frame.strides[1] < static_cast<int32_t>(impl.width / 2) ||
        frame.strides[2] < static_cast<int32_t>(impl.width / 2)) {
        error = "SVT-AV1 currently requires matching positive-stride I420 input";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    EbSvtIOFormat input{};
    input.luma = const_cast<uint8_t*>(frame.planes[0]);
    input.cb = const_cast<uint8_t*>(frame.planes[1]);
    input.cr = const_cast<uint8_t*>(frame.planes[2]);
    input.y_stride = static_cast<uint32_t>(frame.strides[0]);
    input.cb_stride = static_cast<uint32_t>(frame.strides[1]);
    input.cr_stride = static_cast<uint32_t>(frame.strides[2]);
    EbBufferHeaderType header{};
    header.size = sizeof(header);
    header.p_buffer = reinterpret_cast<uint8_t*>(&input);
    header.n_filled_len = impl.width * impl.height * 3 / 2;
    header.pts = frame.pts >= 0 ? frame.pts : impl.next_pts;
    if (svt_av1_enc_send_picture(impl.codec, &header) != EB_ErrorNone) {
        error = "SVT-AV1 rejected an input frame";
        return MKVC_ERROR_CODEC;
    }
    impl.next_pts = std::max(impl.next_pts, header.pts + 1);
    return collect_packets(impl, false, error);
#endif
}

mkvc_result CpuAv1Encoder::flush(std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    error = "CPU AV1 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (impl_->closed) return MKVC_OK;
    return collect_packets(*impl_, false, error);
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
    if (impl.codec_initialized && !impl.eos_sent) {
        EbBufferHeaderType eos{};
        eos.size = sizeof(eos);
        eos.flags = EB_BUFFERFLAG_EOS;
        if (svt_av1_enc_send_picture(impl.codec, &eos) != EB_ErrorNone) {
            error = "SVT-AV1 rejected end-of-stream";
            result = MKVC_ERROR_CODEC;
        } else {
            impl.eos_sent = true;
            result = collect_packets(impl, true, error);
        }
    }
    if (result == MKVC_OK && impl.segment_initialized &&
        !impl.segment.Finalize()) {
        error = "libwebm failed to finalize AV1 output";
        result = MKVC_ERROR_IO;
    }
    if (impl.writer_open) impl.writer.Close();
    if (impl.codec_initialized) svt_av1_enc_deinit(impl.codec);
    if (impl.codec != nullptr) svt_av1_enc_deinit_handle(impl.codec);
    impl.codec = nullptr;
    impl.codec_initialized = false;
    impl.writer_open = false;
    impl.closed = true;
    return result;
#endif
}

}  // namespace mkvc
