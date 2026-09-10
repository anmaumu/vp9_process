#include "cpu_av1_decoder_runtime.hpp"

#include "cpu_av1_decoder_state.hpp"

#if defined(MKVC_HAS_CPU_AV1)
#include <aom/aomdx.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace mkvc::cpu_av1_decoder_detail {
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

mkvc_result initialize(CpuAv1Decoder::Impl& impl, const mkvc_decoder_config& config,
                       std::string& error) {
    impl.packet_reader = WebmPacketReader::open(config.input_path_utf8, MKVC_CODEC_AV1, error);
    if (!impl.packet_reader) return MKVC_ERROR_IO;
    aom_codec_dec_cfg_t codec_config{};
    codec_config.threads = config.threads;
    if (aom_codec_dec_init(&impl.codec, aom_codec_av1_dx(), &codec_config, 0) != AOM_CODEC_OK) {
        error = aom_codec_error(&impl.codec);
        return MKVC_ERROR_CODEC;
    }
    impl.codec_initialized = true;
    return MKVC_OK;
}

mkvc_result read_frame(CpuAv1Decoder::Impl& impl, std::unique_ptr<DecodedFrame>& frame,
                       std::string& error) {
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
                if (submitted != impl.submitted_packets.end())
                    impl.submitted_packets.erase(submitted);
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
        return MKVC_END_OF_STREAM;
    }
}

void destroy(CpuAv1Decoder::Impl& impl) noexcept {
    if (impl.codec_initialized) {
        aom_codec_destroy(&impl.codec);
        impl.codec_initialized = false;
    }
    impl.submitted_packets.clear();
    impl.packet_reader.reset();
}

}  // namespace mkvc::cpu_av1_decoder_detail
#endif
