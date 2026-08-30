#include "cpu_av1_encoder.hpp"

#if defined(MKVC_HAS_CPU_AV1)
#include <svt-av1/EbSvtAv1Enc.h>
#include <libyuv/convert.h>
#include <webm/mkvmuxer/mkvmuxer.h>
#include <webm/mkvmuxer/mkvwriter.h>
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

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
    uint32_t quality = 32;
    uint32_t keyframe_interval_frames = 0;
    int64_t next_pts = 0;
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

void copy_plane(uint8_t* destination, int destination_stride,
                const uint8_t* source, int source_stride,
                uint32_t width, uint32_t height) {
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(destination + row * destination_stride,
                    source + row * source_stride, width);
    }
}

mkvc_result convert_to_i420(CpuAv1Encoder::Impl& impl,
                            const mkvc_frame_view& frame,
                            std::string& error) {
    const size_t y_size = static_cast<size_t>(impl.width) * impl.height;
    const size_t uv_size = static_cast<size_t>(impl.width / 2) *
                           (impl.height / 2);
    uint8_t* y = impl.image.data();
    uint8_t* u = y + y_size;
    uint8_t* v = u + uv_size;
    int conversion_result = 0;
    switch (frame.pixel_format) {
        case MKVC_PIXEL_FORMAT_I420:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.planes[2] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(impl.width) ||
                frame.strides[1] < static_cast<int32_t>(impl.width / 2) ||
                frame.strides[2] < static_cast<int32_t>(impl.width / 2)) {
                error = "I420 requires three positive-stride planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            copy_plane(y, static_cast<int>(impl.width), frame.planes[0],
                       frame.strides[0], impl.width, impl.height);
            copy_plane(u, static_cast<int>(impl.width / 2), frame.planes[1],
                       frame.strides[1], impl.width / 2, impl.height / 2);
            copy_plane(v, static_cast<int>(impl.width / 2), frame.planes[2],
                       frame.strides[2], impl.width / 2, impl.height / 2);
            break;
        case MKVC_PIXEL_FORMAT_NV12:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(impl.width) ||
                frame.strides[1] < static_cast<int32_t>(impl.width)) {
                error = "NV12 requires Y and interleaved UV positive-stride planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            conversion_result = libyuv::NV12ToI420(
                frame.planes[0], frame.strides[0], frame.planes[1],
                frame.strides[1], y, static_cast<int>(impl.width), u,
                static_cast<int>(impl.width / 2), v,
                static_cast<int>(impl.width / 2), static_cast<int>(impl.width),
                static_cast<int>(impl.height));
            break;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
        case MKVC_PIXEL_FORMAT_BGRA32: {
            const uint32_t bytes_per_pixel =
                frame.pixel_format == MKVC_PIXEL_FORMAT_BGRA32 ? 4u : 3u;
            if (frame.planes[0] == nullptr ||
                frame.strides[0] <
                    static_cast<int32_t>(impl.width * bytes_per_pixel)) {
                error = "packed RGB input has an invalid pointer or stride";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            if (frame.pixel_format == MKVC_PIXEL_FORMAT_BGR24) {
                conversion_result = libyuv::RGB24ToI420(
                    frame.planes[0], frame.strides[0], y,
                    static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v,
                    static_cast<int>(impl.width / 2),
                    static_cast<int>(impl.width), static_cast<int>(impl.height));
            } else if (frame.pixel_format == MKVC_PIXEL_FORMAT_RGB24) {
                conversion_result = libyuv::RAWToI420(
                    frame.planes[0], frame.strides[0], y,
                    static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v,
                    static_cast<int>(impl.width / 2),
                    static_cast<int>(impl.width), static_cast<int>(impl.height));
            } else {
                conversion_result = libyuv::ARGBToI420(
                    frame.planes[0], frame.strides[0], y,
                    static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v,
                    static_cast<int>(impl.width / 2),
                    static_cast<int>(impl.width), static_cast<int>(impl.height));
            }
            break;
        }
        default:
            error = "unsupported AV1 input pixel format";
            return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (conversion_result != 0) {
        error = "libyuv failed to convert the AV1 input frame";
        return MKVC_ERROR_INTERNAL;
    }
    return MKVC_OK;
}

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
                error = "libwebm failed to mux AV1 packet pts=" +
                        std::to_string(packet->pts) + " type=" +
                        std::to_string(static_cast<int>(packet->pic_type)) +
                        " flags=" + std::to_string(packet->flags);
                svt_av1_enc_release_out_buffer(&packet);
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

mkvc_result initialize_codec(CpuAv1Encoder::Impl& impl, std::string& error) {
    EbSvtAv1EncConfiguration config{};
    if (svt_av1_enc_init_handle(&impl.codec, &config) != EB_ErrorNone) {
        error = "SVT-AV1 failed to create an encoder handle";
        return MKVC_ERROR_CODEC;
    }
    config.source_width = impl.width;
    config.source_height = impl.height;
    config.frame_rate_numerator = impl.fps_num;
    config.frame_rate_denominator = impl.fps_den;
    config.encoder_bit_depth = 8;
    config.encoder_color_format = EB_YUV420;
    config.level = 63;
    config.rate_control_mode = 0;
    config.qp = impl.quality;
    config.enc_mode = 8;
    config.pred_structure = RANDOM_ACCESS;
    config.intra_period_length = impl.keyframe_interval_frames == 0
        ? static_cast<int32_t>(impl.fps_num * 4 / impl.fps_den) - 1
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
    impl.quality = config.quality;
    impl.keyframe_interval_frames = config.keyframe_interval_frames;
    if (initialize_codec(impl, error) != MKVC_OK) return nullptr;
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
    track->set_codec_id("V_AV1");
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
    const uint64_t y_size = static_cast<uint64_t>(config.width) * config.height;
    const uint64_t uv_size = static_cast<uint64_t>(config.width / 2) *
                             (config.height / 2);
    if (y_size + 2 * uv_size > std::numeric_limits<size_t>::max()) {
        error = "frame dimensions exceed addressable memory";
        return nullptr;
    }
    impl.image.resize(static_cast<size_t>(y_size + 2 * uv_size));
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
    if (frame.width != impl.width || frame.height != impl.height) {
        error = "frame dimensions do not match AV1 encoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const mkvc_result conversion = convert_to_i420(impl, frame, error);
    if (conversion != MKVC_OK) return conversion;
    const size_t y_size = static_cast<size_t>(impl.width) * impl.height;
    const size_t uv_size = static_cast<size_t>(impl.width / 2) *
                           (impl.height / 2);
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
    header.pts = frame.pts >= 0 ? frame.pts : impl.next_pts;
    if (svt_av1_enc_send_picture(impl.codec, &header) != EB_ErrorNone) {
        error = "SVT-AV1 rejected an input frame";
        return MKVC_ERROR_CODEC;
    }
    impl.next_pts = std::max(impl.next_pts, header.pts + 1);
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
    if (result == MKVC_OK && impl.segment_initialized &&
        !impl.segment.Finalize()) {
        error = "libwebm failed to finalize AV1 output";
        result = MKVC_ERROR_IO;
    }
    if (impl.writer_open) impl.writer.Close();
    destroy_codec(impl);
    impl.writer_open = false;
    impl.closed = true;
    return result;
#endif
}

}  // namespace mkvc
