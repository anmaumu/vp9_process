#include "cpu_vp9_encoder.hpp"

#if defined(MKVC_HAS_CPU_VP9)
#include <vpx/vp8cx.h>
#include <vpx/vpx_encoder.h>
#include <libyuv/convert.h>
#include <webm/mkvmuxer/mkvmuxer.h>
#include <webm/mkvmuxer/mkvwriter.h>
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace mkvc {

struct CpuVp9Encoder::Impl {
#if defined(MKVC_HAS_CPU_VP9)
    vpx_codec_ctx_t codec{};
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
    bool closed = false;
    std::vector<uint8_t> image;
};

CpuVp9Encoder::CpuVp9Encoder() : impl_(std::make_unique<Impl>()) {}

CpuVp9Encoder::~CpuVp9Encoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_CPU_VP9)
namespace {

mkvc_result add_packets(CpuVp9Encoder::Impl& impl, bool drain,
                        std::string& error) {
    if (drain) {
        const vpx_codec_err_t status =
            vpx_codec_encode(&impl.codec, nullptr, -1, 1, 0, VPX_DL_GOOD_QUALITY);
        if (status != VPX_CODEC_OK) {
            error = vpx_codec_error_detail(&impl.codec)
                        ? vpx_codec_error_detail(&impl.codec)
                        : vpx_codec_error(&impl.codec);
            return MKVC_ERROR_CODEC;
        }
    }

    vpx_codec_iter_t iterator = nullptr;
    const vpx_codec_cx_pkt_t* packet = nullptr;
    while ((packet = vpx_codec_get_cx_data(&impl.codec, &iterator)) != nullptr) {
        if (packet->kind != VPX_CODEC_CX_FRAME_PKT) {
            continue;
        }
        const auto pts = static_cast<uint64_t>(packet->data.frame.pts);
        const uint64_t timestamp_ns =
            pts * static_cast<uint64_t>(impl.fps_den) * 1000000000ULL /
            impl.fps_num;
        const bool key = (packet->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
        mkvmuxer::Frame frame;
        if (!frame.Init(static_cast<const uint8_t*>(packet->data.frame.buf),
                        packet->data.frame.sz)) {
            error = "libwebm failed to copy an encoded VP9 packet";
            return MKVC_ERROR_INTERNAL;
        }
        frame.set_track_number(impl.track_number);
        frame.set_timestamp(timestamp_ns);
        frame.set_duration(static_cast<uint64_t>(impl.fps_den) * 1000000000ULL /
                           impl.fps_num);
        frame.set_is_key(key);
        if (!impl.segment.AddGenericFrame(&frame)) {
            error = "libwebm failed to mux a VP9 frame";
            return MKVC_ERROR_IO;
        }
    }
    return MKVC_OK;
}

void copy_plane(uint8_t* destination, int destination_stride,
                const uint8_t* source, int source_stride,
                uint32_t width, uint32_t height) {
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(destination + row * destination_stride,
                    source + row * source_stride, width);
    }
}

}  // namespace
#endif

std::unique_ptr<CpuVp9Encoder> CpuVp9Encoder::create(
    const mkvc_encoder_config& config, std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    (void)config;
    error = "CPU VP9 backend was not built";
    return nullptr;
#else
    auto encoder = std::unique_ptr<CpuVp9Encoder>(new CpuVp9Encoder());
    auto& impl = *encoder->impl_;
    impl.width = config.width;
    impl.height = config.height;
    impl.fps_num = config.fps_num;
    impl.fps_den = config.fps_den;

    vpx_codec_enc_cfg_t codec_config{};
    if (vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &codec_config, 0) !=
        VPX_CODEC_OK) {
        error = "libvpx has no default VP9 encoder configuration";
        return nullptr;
    }
    codec_config.g_w = config.width;
    codec_config.g_h = config.height;
    codec_config.g_timebase.num = static_cast<int>(config.fps_den);
    codec_config.g_timebase.den = static_cast<int>(config.fps_num);
    codec_config.g_threads = config.threads == 0
        ? std::max(1u, std::thread::hardware_concurrency())
        : config.threads;
    codec_config.g_lag_in_frames = 0;
    codec_config.kf_mode = VPX_KF_AUTO;
    codec_config.kf_max_dist = config.keyframe_interval_frames == 0
        ? config.fps_num * 4 / config.fps_den
        : config.keyframe_interval_frames;
    codec_config.rc_end_usage = VPX_CQ;
    codec_config.rc_target_bitrate = std::max(
        1u, config.width * config.height * config.fps_num /
                config.fps_den / 1000u);

    if (vpx_codec_enc_init(&impl.codec, vpx_codec_vp9_cx(), &codec_config, 0) !=
        VPX_CODEC_OK) {
        error = vpx_codec_error(&impl.codec);
        return nullptr;
    }
    impl.codec_initialized = true;
    if (vpx_codec_control(&impl.codec, VP8E_SET_CPUUSED, 6) != VPX_CODEC_OK ||
        vpx_codec_control(&impl.codec, VP8E_SET_CQ_LEVEL,
                          static_cast<int>(config.quality)) != VPX_CODEC_OK ||
        vpx_codec_control(&impl.codec, VP9E_SET_ROW_MT, 1) != VPX_CODEC_OK) {
        error = "libvpx rejected the balanced VP9 controls";
        return nullptr;
    }

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
        error = "libwebm failed to create the video track";
        return nullptr;
    }
    track->set_codec_id(mkvmuxer::Tracks::kVp9CodecId);
    track->set_frame_rate(static_cast<double>(config.fps_num) / config.fps_den);
    track->set_default_duration(
        static_cast<uint64_t>(config.fps_den) * 1000000000ULL / config.fps_num);

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

mkvc_result CpuVp9Encoder::write(const mkvc_frame_view& frame,
                                 std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    (void)frame;
    error = "CPU VP9 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) {
        error = "encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (frame.width != impl.width || frame.height != impl.height) {
        error = "frame dimensions do not match encoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }

    const size_t y_size = static_cast<size_t>(impl.width) * impl.height;
    const size_t uv_size = static_cast<size_t>(impl.width / 2) * (impl.height / 2);
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
                frame.planes[0], frame.strides[0], frame.planes[1], frame.strides[1],
                y, static_cast<int>(impl.width), u, static_cast<int>(impl.width / 2),
                v, static_cast<int>(impl.width / 2), static_cast<int>(impl.width),
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
                    static_cast<int>(impl.width / 2), static_cast<int>(impl.width),
                    static_cast<int>(impl.height));
            } else if (frame.pixel_format == MKVC_PIXEL_FORMAT_RGB24) {
                conversion_result = libyuv::RAWToI420(
                    frame.planes[0], frame.strides[0], y,
                    static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v,
                    static_cast<int>(impl.width / 2), static_cast<int>(impl.width),
                    static_cast<int>(impl.height));
            } else {
                conversion_result = libyuv::ARGBToI420(
                    frame.planes[0], frame.strides[0], y,
                    static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v,
                    static_cast<int>(impl.width / 2), static_cast<int>(impl.width),
                    static_cast<int>(impl.height));
            }
            break;
        }
        default:
            error = "unsupported input pixel format";
            return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (conversion_result != 0) {
        error = "libyuv failed to convert the input frame";
        return MKVC_ERROR_INTERNAL;
    }

    vpx_image_t image{};
    if (vpx_img_wrap(&image, VPX_IMG_FMT_I420, impl.width, impl.height, 1,
                     impl.image.data()) == nullptr) {
        error = "libvpx failed to wrap the copied I420 frame";
        return MKVC_ERROR_CODEC;
    }
    const int64_t pts = frame.pts >= 0 ? frame.pts : impl.next_pts;
    const vpx_codec_err_t status = vpx_codec_encode(
        &impl.codec, &image, pts, 1, 0, VPX_DL_GOOD_QUALITY);
    if (status != VPX_CODEC_OK) {
        error = vpx_codec_error_detail(&impl.codec)
                    ? vpx_codec_error_detail(&impl.codec)
                    : vpx_codec_error(&impl.codec);
        return MKVC_ERROR_CODEC;
    }
    impl.next_pts = std::max(impl.next_pts, pts + 1);
    return add_packets(impl, false, error);
#endif
}

mkvc_result CpuVp9Encoder::flush(std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    error = "CPU VP9 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (impl_->closed) {
        return MKVC_OK;
    }
    return add_packets(*impl_, true, error);
#endif
}

mkvc_result CpuVp9Encoder::close(std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    (void)error;
    impl_->closed = true;
    return MKVC_OK;
#else
    if (impl_->closed) {
        return MKVC_OK;
    }
    mkvc_result result = MKVC_OK;
    if (impl_->codec_initialized) {
        result = flush(error);
    }
    if (result == MKVC_OK && impl_->segment_initialized &&
        !impl_->segment.Finalize()) {
        error = "libwebm failed to finalize the output";
        result = MKVC_ERROR_IO;
    }
    if (impl_->writer_open) {
        impl_->writer.Close();
        impl_->writer_open = false;
    }
    if (impl_->codec_initialized) {
        vpx_codec_destroy(&impl_->codec);
        impl_->codec_initialized = false;
    }
    impl_->closed = true;
    return result;
#endif
}

}  // namespace mkvc
