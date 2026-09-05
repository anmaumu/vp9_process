#include "cpu_av1_decoder.hpp"

#include "webm_packet_reader.hpp"

#if defined(MKVC_HAS_CPU_AV1)
#include <aom/aom_decoder.h>
#include <aom/aomdx.h>
#endif

#include <algorithm>
#include <cstring>
#include <list>
#include <utility>

namespace mkvc {

struct CpuAv1Decoder::Impl {
#if defined(MKVC_HAS_CPU_AV1)
    aom_codec_ctx_t codec{};
    bool codec_initialized = false;
    aom_codec_iter_t iterator = nullptr;
    std::unique_ptr<WebmPacketReader> packet_reader;
    std::list<EncodedPacket> submitted_packets;
    bool output_pending = false;
    const int64_t* active_pts = nullptr;
#endif
    bool demux_eos = false;
    bool closed = false;
};

CpuAv1Decoder::CpuAv1Decoder() : impl_(std::make_unique<Impl>()) {}
CpuAv1Decoder::~CpuAv1Decoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_CPU_AV1)
namespace {

std::unique_ptr<DecodedFrame> copy_image(const aom_image_t& image, int64_t pts_ns,
                                         std::string& error) {
    const bool byte_i420 = image.fmt == AOM_IMG_FMT_I420;
    const bool word_i420 = image.fmt == AOM_IMG_FMT_I42016 && image.bit_depth == 8;
    if ((!byte_i420 && !word_i420) || image.d_w == 0 || image.d_h == 0 || (image.d_w & 1u) != 0 ||
        (image.d_h & 1u) != 0) {
        error =
            "libaom produced unsupported format=" + std::to_string(static_cast<int>(image.fmt)) +
            " size=" + std::to_string(image.d_w) + "x" + std::to_string(image.d_h) +
            " bit_depth=" + std::to_string(image.bit_depth);
        return nullptr;
    }
    auto frame = std::make_unique<DecodedFrame>();
    frame->width = image.d_w;
    frame->height = image.d_h;
    frame->pts_ns = pts_ns;
    const size_t y_size = static_cast<size_t>(image.d_w) * image.d_h;
    const size_t uv_size = static_cast<size_t>(image.d_w / 2) * (image.d_h / 2);
    frame->pixels.resize(y_size + 2 * uv_size);
    frame->offsets = {0, y_size, y_size + uv_size};
    frame->strides = {static_cast<int32_t>(image.d_w), static_cast<int32_t>(image.d_w / 2),
                      static_cast<int32_t>(image.d_w / 2)};
    for (uint32_t plane = 0; plane < 3; ++plane) {
        const uint32_t width = plane == 0 ? image.d_w : image.d_w / 2;
        const uint32_t height = plane == 0 ? image.d_h : image.d_h / 2;
        uint8_t* destination = frame->pixels.data() + frame->offsets[plane];
        for (uint32_t row = 0; row < height; ++row) {
            if (byte_i420) {
                std::memcpy(destination + row * frame->strides[plane],
                            image.planes[plane] + row * image.stride[plane], width);
            } else {
                const auto* source = reinterpret_cast<const uint16_t*>(image.planes[plane] +
                                                                       row * image.stride[plane]);
                for (uint32_t column = 0; column < width; ++column) {
                    destination[row * frame->strides[plane] + column] =
                        static_cast<uint8_t>(source[column]);
                }
            }
        }
    }
    return frame;
}

}  // namespace
#endif

std::unique_ptr<CpuAv1Decoder> CpuAv1Decoder::create(const mkvc_decoder_config& config,
                                                     std::string& error) {
#if !defined(MKVC_HAS_CPU_AV1)
    (void)config;
    error = "CPU AV1 backend was not built";
    return nullptr;
#else
    auto decoder = std::unique_ptr<CpuAv1Decoder>(new CpuAv1Decoder());
    decoder->impl_->packet_reader =
        WebmPacketReader::open(config.input_path_utf8, MKVC_CODEC_AV1, error);
    if (!decoder->impl_->packet_reader) {
        return nullptr;
    }
    aom_codec_dec_cfg_t codec_config{};
    codec_config.threads = config.threads;
    if (aom_codec_dec_init(&decoder->impl_->codec, aom_codec_av1_dx(), &codec_config, 0) !=
        AOM_CODEC_OK) {
        error = aom_codec_error(&decoder->impl_->codec);
        return nullptr;
    }
    decoder->impl_->codec_initialized = true;
    return decoder;
#endif
}

mkvc_result CpuAv1Decoder::read(std::unique_ptr<DecodedFrame>& frame, std::string& error) {
    frame.reset();
#if !defined(MKVC_HAS_CPU_AV1)
    error = "CPU AV1 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) {
        error = "decoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    while (true) {
        if (impl.output_pending) {
            if (aom_image_t* image = aom_codec_get_frame(&impl.codec, &impl.iterator)) {
                const auto* pts_pointer = static_cast<const int64_t*>(image->user_priv);
                const int64_t pts_ns = pts_pointer == nullptr ? 0 : *pts_pointer;
                frame = copy_image(*image, pts_ns, error);
                return frame ? MKVC_OK : MKVC_ERROR_NOT_SUPPORTED;
            }
            impl.output_pending = false;
            impl.iterator = nullptr;
            if (impl.active_pts != nullptr) {
                const auto submitted =
                    std::find_if(impl.submitted_packets.begin(), impl.submitted_packets.end(),
                                 [&impl](const EncodedPacket& packet) {
                                     return &packet.pts_ns == impl.active_pts;
                                 });
                if (submitted != impl.submitted_packets.end()) {
                    impl.submitted_packets.erase(submitted);
                }
                impl.active_pts = nullptr;
            }
        }
        if (!impl.demux_eos) {
            EncodedPacket packet;
            const mkvc_result demux_result = impl.packet_reader->read(packet, error);
            if (demux_result != MKVC_OK) {
                if (demux_result != MKVC_END_OF_STREAM) return demux_result;
                impl.demux_eos = true;
                continue;
            }
            impl.submitted_packets.push_back(std::move(packet));
            auto& submitted = impl.submitted_packets.back();
            if (aom_codec_decode(&impl.codec, submitted.data.data(), submitted.data.size(),
                                 &submitted.pts_ns) != AOM_CODEC_OK) {
                error = aom_codec_error_detail(&impl.codec) ? aom_codec_error_detail(&impl.codec)
                                                            : aom_codec_error(&impl.codec);
                return MKVC_ERROR_CODEC;
            }
            impl.output_pending = true;
            impl.iterator = nullptr;
            impl.active_pts = &submitted.pts_ns;
            continue;
        }
        // libaom's AV1 decoder exposes output synchronously for each submitted
        // temporal unit; unlike the libvpx path it must not be null-drained.
        return MKVC_END_OF_STREAM;
    }
#endif
}

mkvc_result CpuAv1Decoder::close(std::string& error) {
    (void)error;
    if (impl_->closed) return MKVC_OK;
#if defined(MKVC_HAS_CPU_AV1)
    if (impl_->codec_initialized) {
        aom_codec_destroy(&impl_->codec);
        impl_->codec_initialized = false;
    }
    impl_->submitted_packets.clear();
    impl_->packet_reader.reset();
#endif
    impl_->closed = true;
    return MKVC_OK;
}

}  // namespace mkvc
