#include "intel_webm_encoder.hpp"

#include "gpu/gpu_frame.hpp"

#if defined(MKVC_HAS_INTEL_ONEVPL)
#include <libyuv/convert.h>
#include <libyuv/planar_functions.h>

#include "gpu/intel/vpl_encoder_sequence.hpp"
#include "gpu/intel/vpl_packet_muxer.hpp"
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace mkvc {

struct IntelWebmEncoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    std::unique_ptr<gpu::intel::VplPacketMuxer> muxer;
    std::unique_ptr<gpu::intel::VplEncoderSequence> sequence;
#endif
    uint32_t width = 0;
    uint32_t height = 0;
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

void copy_plane(uint8_t* destination, int destination_stride, const uint8_t* source,
                int source_stride, uint32_t width, uint32_t height) {
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(destination + static_cast<size_t>(row) * destination_stride,
                    source + static_cast<size_t>(row) * source_stride, width);
    }
}

mkvc_result convert_to_nv12(IntelWebmEncoder::Impl& impl, const mkvc_frame_view& frame,
                            std::string& error) {
    const size_t y_size = static_cast<size_t>(impl.width) * impl.height;
    const size_t uv_size = static_cast<size_t>(impl.width / 2) * (impl.height / 2);
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
            copy_plane(nv_y, static_cast<int>(impl.width), frame.planes[0], frame.strides[0],
                       impl.width, impl.height);
            copy_plane(nv_uv, static_cast<int>(impl.width), frame.planes[1], frame.strides[1],
                       impl.width, impl.height / 2);
            return MKVC_OK;
        case MKVC_PIXEL_FORMAT_I420:
            if (frame.planes[0] == nullptr || frame.planes[1] == nullptr ||
                frame.planes[2] == nullptr || frame.strides[0] < static_cast<int32_t>(impl.width) ||
                frame.strides[1] < static_cast<int32_t>(impl.width / 2) ||
                frame.strides[2] < static_cast<int32_t>(impl.width / 2)) {
                error = "I420 requires three valid planes";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            conversion = libyuv::I420ToNV12(
                frame.planes[0], frame.strides[0], frame.planes[1], frame.strides[1],
                frame.planes[2], frame.strides[2], nv_y, static_cast<int>(impl.width), nv_uv,
                static_cast<int>(impl.width), static_cast<int>(impl.width),
                static_cast<int>(impl.height));
            break;
        case MKVC_PIXEL_FORMAT_BGR24:
        case MKVC_PIXEL_FORMAT_RGB24:
        case MKVC_PIXEL_FORMAT_BGRA32: {
            const uint32_t channels = frame.pixel_format == MKVC_PIXEL_FORMAT_BGRA32 ? 4u : 3u;
            if (frame.planes[0] == nullptr ||
                frame.strides[0] < static_cast<int32_t>(impl.width * channels)) {
                error = "packed RGB input has an invalid plane or stride";
                return MKVC_ERROR_INVALID_ARGUMENT;
            }
            if (frame.pixel_format == MKVC_PIXEL_FORMAT_BGR24) {
                conversion = libyuv::RGB24ToI420(
                    frame.planes[0], frame.strides[0], y, static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v, static_cast<int>(impl.width / 2),
                    static_cast<int>(impl.width), static_cast<int>(impl.height));
            } else if (frame.pixel_format == MKVC_PIXEL_FORMAT_RGB24) {
                conversion = libyuv::RAWToI420(
                    frame.planes[0], frame.strides[0], y, static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v, static_cast<int>(impl.width / 2),
                    static_cast<int>(impl.width), static_cast<int>(impl.height));
            } else {
                conversion = libyuv::ARGBToI420(
                    frame.planes[0], frame.strides[0], y, static_cast<int>(impl.width), u,
                    static_cast<int>(impl.width / 2), v, static_cast<int>(impl.width / 2),
                    static_cast<int>(impl.width), static_cast<int>(impl.height));
            }
            if (conversion == 0) {
                conversion = libyuv::I420ToNV12(
                    y, static_cast<int>(impl.width), u, static_cast<int>(impl.width / 2), v,
                    static_cast<int>(impl.width / 2), nv_y, static_cast<int>(impl.width), nv_uv,
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

}  // namespace
#endif

std::unique_ptr<IntelWebmEncoder> IntelWebmEncoder::create(const mkvc_encoder_config& config,
                                                           std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)config;
    error = "Intel oneVPL backend was not built";
    return nullptr;
#else
    auto encoder = std::unique_ptr<IntelWebmEncoder>(new IntelWebmEncoder());
    auto& impl = *encoder->impl_;
    impl.width = config.width;
    impl.height = config.height;
    const gpu::intel::VplEncoderSequenceConfig sequence_config{config.codec,
                                                               config.width,
                                                               config.height,
                                                               config.fps_num,
                                                               config.fps_den,
                                                               config.quality,
                                                               config.keyframe_interval_frames};
    impl.sequence = gpu::intel::VplEncoderSequence::create(sequence_config, error);
    if (!impl.sequence) return nullptr;
    impl.muxer = gpu::intel::VplPacketMuxer::create(config, error);
    if (!impl.muxer) return nullptr;
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

mkvc_result IntelWebmEncoder::write(const mkvc_frame_view& frame, std::string& error) {
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
    result = impl.sequence->write_nv12(impl.nv12.data(), static_cast<int32_t>(impl.width),
                                       impl.nv12.data() + y_size, static_cast<int32_t>(impl.width),
                                       frame.pts, packets, error);
    if (result != MKVC_OK) return result;
    return impl.muxer->write(packets, error);
#endif
}

mkvc_result IntelWebmEncoder::write_gpu(const std::shared_ptr<gpu::GpuFrameCore>& frame,
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
    if (!frame || frame->desc().width != impl.width || frame->desc().height != impl.height) {
        error = "GPU frame dimensions do not match Intel encoder";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    std::vector<IntelEncodedPacket> packets;
    const mkvc_result result = impl.sequence->write_gpu(frame, packets, error);
    if (result != MKVC_OK) return result;
    return impl.muxer->write(packets, error);
#endif
}

mkvc_result IntelWebmEncoder::flush(std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) return MKVC_OK;
    std::vector<IntelEncodedPacket> packets;
    mkvc_result result = impl.sequence->flush(packets, error);
    if (result == MKVC_OK) result = impl.muxer->write(packets, error);
    if (result == MKVC_OK) result = impl.sequence->restart_cpu(error);
    return result;
#endif
}

mkvc_result IntelWebmEncoder::close(std::string& error) {
    if (impl_->closed) return MKVC_OK;
    mkvc_result result = MKVC_OK;
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)error;
#else
    auto& impl = *impl_;
    if (impl.sequence) {
        std::vector<IntelEncodedPacket> packets;
        result = impl.sequence->close(packets, error);
        if (result == MKVC_OK && impl.muxer) result = impl.muxer->write(packets, error);
    }
    if (result == MKVC_OK && impl.muxer) result = impl.muxer->finalize(error);
#endif
    impl_->closed = true;
    return result;
}

uint32_t IntelWebmEncoder::max_pending_observed() const {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    return impl_->sequence ? impl_->sequence->max_pending_observed() : 0;
#else
    return 0;
#endif
}

}  // namespace mkvc
