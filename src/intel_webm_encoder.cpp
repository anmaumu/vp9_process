#include "intel_webm_encoder.hpp"
#include "gpu/gpu_frame.hpp"

#if defined(MKVC_HAS_INTEL_ONEVPL)
#include <libyuv/convert.h>
#include <libyuv/planar_functions.h>
#include <webm/mkvmuxer/mkvmuxer.h>
#include <webm/mkvmuxer/mkvwriter.h>
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace mkvc {

struct IntelWebmEncoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    mkvmuxer::MkvWriter writer;
    bool writer_open = false;
    mkvmuxer::Segment segment;
    bool segment_initialized = false;
    uint64_t track_number = 0;
#endif
    std::unique_ptr<IntelVplEncoder> encoder;
    uint32_t codec = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    uint32_t quality = 0;
    uint32_t keyframe_interval_frames = 0;
    int64_t next_pts = 0;
    uint64_t frames_in_sequence = 0;
    uint32_t hardware_pending_peak = 0;
    bool closed = false;
    std::vector<uint8_t> i420;
    std::vector<uint8_t> nv12;
};

IntelWebmEncoder::IntelWebmEncoder() : impl_(std::make_unique<Impl>()) {}
IntelWebmEncoder::~IntelWebmEncoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_INTEL_ONEVPL)
namespace {

void copy_plane(uint8_t* destination, int destination_stride,
                const uint8_t* source, int source_stride,
                uint32_t width, uint32_t height) {
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(destination + static_cast<size_t>(row) * destination_stride,
                    source + static_cast<size_t>(row) * source_stride, width);
    }
}

mkvc_result convert_to_nv12(IntelWebmEncoder::Impl& impl,
                            const mkvc_frame_view& frame,
                            std::string& error) {
    const size_t y_size = static_cast<size_t>(impl.width) * impl.height;
    const size_t uv_size = static_cast<size_t>(impl.width / 2) *
                           (impl.height / 2);
    uint8_t* y = impl.i420.data();
    uint8_t* u = y + y_size;
    uint8_t* v = u + uv_size;
    uint8_t* nv_y = impl.nv12.data();
    uint8_t* nv_uv = nv_y + y_size;
    int conversion = 0;
    switch (frame.pixel_format) {
        case MKVC_PIXEL_FORMAT_NV12:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(impl.width) ||
                frame.strides[1] < static_cast<int32_t>(impl.width)) {
                error = "NV12 requires valid Y and UV planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            copy_plane(nv_y, static_cast<int>(impl.width), frame.planes[0],
                       frame.strides[0], impl.width, impl.height);
            copy_plane(nv_uv, static_cast<int>(impl.width), frame.planes[1],
                       frame.strides[1], impl.width, impl.height / 2);
            return MKVC_OK;
        case MKVC_PIXEL_FORMAT_I420:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.planes[2] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(impl.width) ||
                frame.strides[1] < static_cast<int32_t>(impl.width / 2) ||
                frame.strides[2] < static_cast<int32_t>(impl.width / 2)) {
                error = "I420 requires three valid planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            conversion = libyuv::I420ToNV12(
                frame.planes[0], frame.strides[0], frame.planes[1],
                frame.strides[1], frame.planes[2], frame.strides[2], nv_y,
                static_cast<int>(impl.width), nv_uv,
                static_cast<int>(impl.width), static_cast<int>(impl.width),
                static_cast<int>(impl.height));
            break;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
        case MKVC_PIXEL_FORMAT_BGRA32: {
            const uint32_t channels =
                frame.pixel_format == MKVC_PIXEL_FORMAT_BGRA32 ? 4u : 3u;
            if (frame.planes[0] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(impl.width * channels)) {
                error = "packed RGB input has an invalid plane or stride";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            if (frame.pixel_format == MKVC_PIXEL_FORMAT_BGR24) {
                conversion = libyuv::RGB24ToI420(
                    frame.planes[0], frame.strides[0], y,
                    static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v,
                    static_cast<int>(impl.width / 2),
                    static_cast<int>(impl.width), static_cast<int>(impl.height));
            } else if (frame.pixel_format == MKVC_PIXEL_FORMAT_RGB24) {
                conversion = libyuv::RAWToI420(
                    frame.planes[0], frame.strides[0], y,
                    static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v,
                    static_cast<int>(impl.width / 2),
                    static_cast<int>(impl.width), static_cast<int>(impl.height));
            } else {
                conversion = libyuv::ARGBToI420(
                    frame.planes[0], frame.strides[0], y,
                    static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v,
                    static_cast<int>(impl.width / 2),
                    static_cast<int>(impl.width), static_cast<int>(impl.height));
            }
            if (conversion == 0) {
                conversion = libyuv::I420ToNV12(
                    y, static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v,
                    static_cast<int>(impl.width / 2), nv_y,
                    static_cast<int>(impl.width), nv_uv,
                    static_cast<int>(impl.width), static_cast<int>(impl.width),
                    static_cast<int>(impl.height));
            }
            break;
        }
        default:
            error = "unsupported Intel encoder input format";
            return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (conversion != 0) {
        error = "libyuv failed to convert Intel encoder input to NV12";
        return MKVC_ERROR_INTERNAL;
    }
    return MKVC_OK;
}

mkvc_result mux_packets(IntelWebmEncoder::Impl& impl,
                        const std::vector<IntelEncodedPacket>& packets,
                        std::string& error) {
    for (const auto& packet : packets) {
        mkvmuxer::Frame frame;
        if (!frame.Init(packet.data.data(), packet.data.size())) {
            error = "libwebm failed to copy an Intel encoded packet";
            return MKVC_ERROR_INTERNAL;
        }
        frame.set_track_number(impl.track_number);
        frame.set_timestamp(static_cast<uint64_t>(packet.pts) * impl.fps_den *
                            1000000000ULL / impl.fps_num);
        frame.set_duration(static_cast<uint64_t>(impl.fps_den) * 1000000000ULL /
                           impl.fps_num);
        frame.set_is_key(packet.key);
        if (!impl.segment.AddGenericFrame(&frame)) {
            error = "libwebm failed to mux an Intel encoded packet";
            return MKVC_ERROR_IO;
        }
    }
    return MKVC_OK;
}

std::unique_ptr<IntelVplEncoder> create_adapter(
    const IntelWebmEncoder::Impl& impl, std::string& error) {
    return IntelVplEncoder::create(
        impl.codec, impl.width, impl.height, impl.fps_num, impl.fps_den,
        impl.quality, impl.keyframe_interval_frames, error);
}

}  // namespace
#endif

std::unique_ptr<IntelWebmEncoder> IntelWebmEncoder::create(
    const mkvc_encoder_config& config, std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)config;
    error = "Intel oneVPL backend was not built";
    return nullptr;
#else
    auto encoder = std::unique_ptr<IntelWebmEncoder>(new IntelWebmEncoder());
    auto& impl = *encoder->impl_;
    impl.codec = config.codec;
    impl.width = config.width;
    impl.height = config.height;
    impl.fps_num = config.fps_num;
    impl.fps_den = config.fps_den;
    impl.quality = config.quality;
    impl.keyframe_interval_frames = config.keyframe_interval_frames;
    impl.encoder = create_adapter(impl, error);
    if (!impl.encoder) return nullptr;
    if (!impl.writer.Open(config.output_path_utf8)) {
        error = "failed to open Intel encoder output";
        return nullptr;
    }
    impl.writer_open = true;
    if (!impl.segment.Init(&impl.writer)) {
        error = "failed to initialize Intel libwebm muxer";
        return nullptr;
    }
    impl.segment_initialized = true;
    impl.segment.set_mode(mkvmuxer::Segment::kFile);
    impl.track_number = impl.segment.AddVideoTrack(
        static_cast<int32_t>(config.width), static_cast<int32_t>(config.height), 0);
    auto* track = static_cast<mkvmuxer::VideoTrack*>(
        impl.segment.GetTrackByNumber(impl.track_number));
    if (impl.track_number == 0 || track == nullptr) {
        error = "libwebm failed to create the Intel video track";
        return nullptr;
    }
    track->set_codec_id(config.codec == MKVC_CODEC_VP9 ? "V_VP9" : "V_AV1");
    if (config.codec == MKVC_CODEC_AV1) {
        const uint8_t av1_config[4] = {0x81, 19, 0x0c, 0x00};
        if (!track->SetCodecPrivate(av1_config, sizeof(av1_config))) {
            error = "libwebm rejected Intel AV1 codec configuration";
            return nullptr;
        }
    }
    track->set_frame_rate(static_cast<double>(config.fps_num) / config.fps_den);
    track->set_default_duration(static_cast<uint64_t>(config.fps_den) *
                                1000000000ULL / config.fps_num);
    const uint64_t y_size = static_cast<uint64_t>(config.width) * config.height;
    if (y_size * 3 / 2 > std::numeric_limits<size_t>::max()) {
        error = "Intel frame dimensions exceed addressable memory";
        return nullptr;
    }
    impl.i420.resize(static_cast<size_t>(y_size * 3 / 2));
    impl.nv12.resize(static_cast<size_t>(y_size * 3 / 2));
    return encoder;
#endif
}

mkvc_result IntelWebmEncoder::write(const mkvc_frame_view& frame,
                                    std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)frame;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) {
        error = "Intel encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (frame.width != impl.width || frame.height != impl.height) {
        error = "frame dimensions do not match Intel encoder";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    mkvc_result result = convert_to_nv12(impl, frame, error);
    if (result != MKVC_OK) return result;
    const size_t y_size = static_cast<size_t>(impl.width) * impl.height;
    std::vector<IntelEncodedPacket> packets;
    const int64_t frame_pts = frame.pts >= 0 ? frame.pts : impl.next_pts;
    result = impl.encoder->write_nv12(
        impl.nv12.data(), static_cast<int32_t>(impl.width),
        impl.nv12.data() + y_size, static_cast<int32_t>(impl.width), frame_pts,
        packets, error);
    if (result != MKVC_OK) return result;
    impl.next_pts = std::max(impl.next_pts, frame_pts + 1);
    ++impl.frames_in_sequence;
    return mux_packets(impl, packets, error);
#endif
}

mkvc_result IntelWebmEncoder::write_gpu(
    const std::shared_ptr<gpu::GpuFrameCore>& frame, std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)frame;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) {
        error = "Intel encoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    if (!frame || frame->desc().width != impl.width ||
        frame->desc().height != impl.height) {
        error = "GPU frame dimensions do not match Intel encoder";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    std::vector<IntelEncodedPacket> packets;
    const int64_t frame_pts = impl.next_pts;
    const mkvc_result result = impl.encoder->write_gpu_surface(
        frame, frame_pts, packets, error);
    if (result != MKVC_OK) return result;
    impl.next_pts = frame_pts + 1;
    ++impl.frames_in_sequence;
    return mux_packets(impl, packets, error);
#endif
}

mkvc_result IntelWebmEncoder::flush(std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed || impl.frames_in_sequence == 0) return MKVC_OK;
    std::vector<IntelEncodedPacket> packets;
    mkvc_result result = impl.encoder->drain(packets, error);
    if (result == MKVC_OK) result = mux_packets(impl, packets, error);
    std::string close_error;
    impl.hardware_pending_peak = std::max(
        impl.hardware_pending_peak, impl.encoder->max_pending_observed());
    impl.encoder->close(close_error);
    impl.encoder.reset();
    impl.frames_in_sequence = 0;
    if (result != MKVC_OK) return result;
    impl.encoder = create_adapter(impl, error);
    return impl.encoder ? MKVC_OK : MKVC_ERROR_CODEC;
#endif
}

mkvc_result IntelWebmEncoder::close(std::string& error) {
    if (impl_->closed) return MKVC_OK;
    mkvc_result result = MKVC_OK;
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)error;
#else
    auto& impl = *impl_;
    if (impl.encoder && impl.frames_in_sequence > 0) {
        std::vector<IntelEncodedPacket> packets;
        result = impl.encoder->drain(packets, error);
        if (result == MKVC_OK) result = mux_packets(impl, packets, error);
    }
    if (impl.encoder) {
        impl.hardware_pending_peak = std::max(
            impl.hardware_pending_peak, impl.encoder->max_pending_observed());
        std::string close_error;
        impl.encoder->close(close_error);
        impl.encoder.reset();
    }
    if (result == MKVC_OK && impl.segment_initialized &&
        !impl.segment.Finalize()) {
        error = "libwebm failed to finalize Intel output";
        result = MKVC_ERROR_IO;
    }
    if (impl.writer_open) impl.writer.Close();
    impl.writer_open = false;
#endif
    impl_->closed = true;
    return result;
}

uint32_t IntelWebmEncoder::max_pending_observed() const {
    return std::max(impl_->hardware_pending_peak,
                    impl_->encoder ? impl_->encoder->max_pending_observed() : 0);
}

}  // namespace mkvc
