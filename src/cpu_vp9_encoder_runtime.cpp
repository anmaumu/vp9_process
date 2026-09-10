#include "cpu_vp9_encoder_runtime.hpp"

#include "cpu_vp9_encoder_state.hpp"

#if defined(MKVC_HAS_CPU_VP9)
#include <vpx/vp8cx.h>

#include <algorithm>
#include <thread>

namespace mkvc::cpu_vp9_detail {
namespace {

mkvc_result codec_error(CpuVp9Encoder::Impl& impl, std::string& error) {
    error = vpx_codec_error_detail(&impl.codec) ? vpx_codec_error_detail(&impl.codec)
                                                : vpx_codec_error(&impl.codec);
    return MKVC_ERROR_CODEC;
}

mkvc_result collect_packets(CpuVp9Encoder::Impl& impl, std::string& error) {
    vpx_codec_iter_t iterator = nullptr;
    const vpx_codec_cx_pkt_t* packet = nullptr;
    while ((packet = vpx_codec_get_cx_data(&impl.codec, &iterator)) != nullptr) {
        if (packet->kind != VPX_CODEC_CX_FRAME_PKT) continue;
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

mkvc_result initialize_codec(CpuVp9Encoder::Impl& impl, const mkvc_encoder_config& config,
                             std::string& error) {
    vpx_codec_enc_cfg_t codec_config{};
    if (vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &codec_config, 0) != VPX_CODEC_OK) {
        error = "libvpx has no default VP9 encoder configuration";
        return MKVC_ERROR_CODEC;
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
        return MKVC_ERROR_CODEC;
    }
    impl.codec_initialized = true;
    if (vpx_codec_control(&impl.codec, VP8E_SET_CPUUSED, 6) != VPX_CODEC_OK ||
        vpx_codec_control(&impl.codec, VP8E_SET_CQ_LEVEL, static_cast<int>(config.quality)) !=
            VPX_CODEC_OK ||
        vpx_codec_control(&impl.codec, VP9E_SET_ROW_MT, 1) != VPX_CODEC_OK) {
        error = "libvpx rejected the balanced VP9 controls";
        return MKVC_ERROR_CODEC;
    }
    return MKVC_OK;
}

mkvc_result encode_frame(CpuVp9Encoder::Impl& impl, int64_t requested_pts, std::string& error) {
    vpx_image_t image{};
    if (vpx_img_wrap(&image, VPX_IMG_FMT_I420, impl.width, impl.height, 1, impl.image.data()) ==
        nullptr) {
        error = "libvpx failed to wrap the copied I420 frame";
        return MKVC_ERROR_CODEC;
    }
    const int64_t pts = impl.timing.select(requested_pts);
    if (vpx_codec_encode(&impl.codec, &image, pts, 1, 0, VPX_DL_GOOD_QUALITY) != VPX_CODEC_OK) {
        return codec_error(impl, error);
    }
    impl.timing.commit(pts);
    return collect_packets(impl, error);
}

mkvc_result flush_codec(CpuVp9Encoder::Impl& impl, std::string& error) {
    if (vpx_codec_encode(&impl.codec, nullptr, -1, 1, 0, VPX_DL_GOOD_QUALITY) != VPX_CODEC_OK) {
        return codec_error(impl, error);
    }
    return collect_packets(impl, error);
}

void destroy_codec(CpuVp9Encoder::Impl& impl) {
    if (!impl.codec_initialized) return;
    vpx_codec_destroy(&impl.codec);
    impl.codec_initialized = false;
}

}  // namespace mkvc::cpu_vp9_detail

#endif
