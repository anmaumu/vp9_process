#include "cpu_vp9_encoder.hpp"

#include "encoder/cpu_frame_to_i420.hpp"
#include "encoder/frame_timing.hpp"
#include "webm_muxer.hpp"

#if defined(MKVC_HAS_CPU_VP9)
#include <vpx/vp8cx.h>
#include <vpx/vpx_encoder.h>
#endif

#include <algorithm>
#include <limits>
#include <thread>
#include <vector>

namespace mkvc {

struct CpuVp9Encoder::Impl {
#if defined(MKVC_HAS_CPU_VP9)
    vpx_codec_ctx_t codec{};
    bool codec_initialized = false;
    std::unique_ptr<WebmMuxer> muxer;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
    encoder::FrameTiming timing;
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

mkvc_result add_packets(CpuVp9Encoder::Impl& impl, bool drain, std::string& error) {
    if (drain) {
        const vpx_codec_err_t status =
            vpx_codec_encode(&impl.codec, nullptr, -1, 1, 0, VPX_DL_GOOD_QUALITY);
        if (status != VPX_CODEC_OK) {
            error = vpx_codec_error_detail(&impl.codec) ? vpx_codec_error_detail(&impl.codec)
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
        const bool key = (packet->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
        const mkvc_result result = impl.muxer->add_frame(
            static_cast<const uint8_t*>(packet->data.frame.buf), packet->data.frame.sz,
            impl.timing.to_nanoseconds(static_cast<int64_t>(pts)),
            impl.timing.duration_nanoseconds(), key, error);
        if (result != MKVC_OK) return result;
    }
    return MKVC_OK;
}

}  // namespace
#endif

std::unique_ptr<CpuVp9Encoder> CpuVp9Encoder::create(const mkvc_encoder_config& config,
                                                     std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    (void)config;
    error = "CPU VP9 backend was not built";
    return nullptr;
#else
    auto encoder = std::unique_ptr<CpuVp9Encoder>(new CpuVp9Encoder());
    auto& impl = *encoder->impl_;
    impl.width = config.width;
    impl.height = config.height;
    impl.timing.configure(config.fps_num, config.fps_den);

    vpx_codec_enc_cfg_t codec_config{};
    if (vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &codec_config, 0) != VPX_CODEC_OK) {
        error = "libvpx has no default VP9 encoder configuration";
        return nullptr;
    }
    codec_config.g_w = config.width;
    codec_config.g_h = config.height;
    codec_config.g_timebase.num = static_cast<int>(config.fps_den);
    codec_config.g_timebase.den = static_cast<int>(config.fps_num);
    codec_config.g_threads =
        config.threads == 0 ? std::max(1u, std::thread::hardware_concurrency()) : config.threads;
    codec_config.g_lag_in_frames = 0;
    codec_config.kf_mode = VPX_KF_AUTO;
    codec_config.kf_max_dist = config.keyframe_interval_frames == 0
                                   ? config.fps_num * 4 / config.fps_den
                                   : config.keyframe_interval_frames;
    codec_config.rc_end_usage = VPX_CQ;
    codec_config.rc_target_bitrate =
        std::max(1u, config.width * config.height * config.fps_num / config.fps_den / 1000u);

    if (vpx_codec_enc_init(&impl.codec, vpx_codec_vp9_cx(), &codec_config, 0) != VPX_CODEC_OK) {
        error = vpx_codec_error(&impl.codec);
        return nullptr;
    }
    impl.codec_initialized = true;
    if (vpx_codec_control(&impl.codec, VP8E_SET_CPUUSED, 6) != VPX_CODEC_OK ||
        vpx_codec_control(&impl.codec, VP8E_SET_CQ_LEVEL, static_cast<int>(config.quality)) !=
            VPX_CODEC_OK ||
        vpx_codec_control(&impl.codec, VP9E_SET_ROW_MT, 1) != VPX_CODEC_OK) {
        error = "libvpx rejected the balanced VP9 controls";
        return nullptr;
    }

    impl.muxer = WebmMuxer::create(config.output_path_utf8, MKVC_CODEC_VP9, config.width,
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

mkvc_result CpuVp9Encoder::write(const mkvc_frame_view& frame, std::string& error) {
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

    const mkvc_result conversion = encoder::convert_cpu_frame_to_i420(
        frame, impl.width, impl.height, impl.image, "VP9", error);
    if (conversion != MKVC_OK) return conversion;

    vpx_image_t image{};
    if (vpx_img_wrap(&image, VPX_IMG_FMT_I420, impl.width, impl.height, 1, impl.image.data()) ==
        nullptr) {
        error = "libvpx failed to wrap the copied I420 frame";
        return MKVC_ERROR_CODEC;
    }
    const int64_t pts = impl.timing.select(frame.pts);
    const vpx_codec_err_t status =
        vpx_codec_encode(&impl.codec, &image, pts, 1, 0, VPX_DL_GOOD_QUALITY);
    if (status != VPX_CODEC_OK) {
        error = vpx_codec_error_detail(&impl.codec) ? vpx_codec_error_detail(&impl.codec)
                                                    : vpx_codec_error(&impl.codec);
        return MKVC_ERROR_CODEC;
    }
    impl.timing.commit(pts);
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
    if (result == MKVC_OK && impl_->muxer) result = impl_->muxer->finalize(error);
    if (impl_->codec_initialized) {
        vpx_codec_destroy(&impl_->codec);
        impl_->codec_initialized = false;
    }
    impl_->closed = true;
    return result;
#endif
}

}  // namespace mkvc
