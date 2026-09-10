#include "cpu_vp9_decoder_runtime.hpp"

#include "cpu_vp9_decoder_state.hpp"

#if defined(MKVC_HAS_CPU_VP9)
#include <vpx/vp8dx.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace mkvc::cpu_vp9_decoder_detail {
namespace {

std::unique_ptr<DecodedFrame> copy_image(const vpx_image_t& image, int64_t pts_ns,
                                         std::string& error) {
    if (image.fmt != VPX_IMG_FMT_I420 || image.d_w == 0 || image.d_h == 0 ||
        (image.d_w & 1u) != 0 || (image.d_h & 1u) != 0) {
        error = "decoder produced an unsupported pixel format or dimension";
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
            std::memcpy(destination + row * frame->strides[plane],
                        image.planes[plane] + row * image.stride[plane], width);
        }
    }
    return frame;
}

}  // namespace

mkvc_result initialize(CpuVp9Decoder::Impl& impl, const mkvc_decoder_config& config,
                       std::string& error) {
    impl.packet_reader = WebmPacketReader::open(config.input_path_utf8, MKVC_CODEC_VP9, error);
    if (!impl.packet_reader) return MKVC_ERROR_IO;
    vpx_codec_dec_cfg_t codec_config{};
    codec_config.threads = config.threads;
    if (vpx_codec_dec_init(&impl.codec, vpx_codec_vp9_dx(), &codec_config, 0) != VPX_CODEC_OK) {
        error = vpx_codec_error(&impl.codec);
        return MKVC_ERROR_CODEC;
    }
    impl.codec_initialized = true;
    return MKVC_OK;
}

mkvc_result read_frame(CpuVp9Decoder::Impl& impl, std::unique_ptr<DecodedFrame>& frame,
                       std::string& error) {
    while (true) {
        if (vpx_image_t* image = vpx_codec_get_frame(&impl.codec, &impl.iterator)) {
            const auto* pts_pointer = static_cast<const int64_t*>(image->user_priv);
            const int64_t pts_ns = pts_pointer == nullptr ? 0 : *pts_pointer;
            frame = copy_image(*image, pts_ns, error);
            if (pts_pointer != nullptr) {
                const auto submitted =
                    std::find_if(impl.submitted_packets.begin(), impl.submitted_packets.end(),
                                 [pts_pointer](const EncodedPacket& packet) {
                                     return &packet.pts_ns == pts_pointer;
                                 });
                if (submitted != impl.submitted_packets.end())
                    impl.submitted_packets.erase(submitted);
            }
            return frame ? MKVC_OK : MKVC_ERROR_NOT_SUPPORTED;
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
            if (vpx_codec_decode(&impl.codec, submitted.data.data(),
                                 static_cast<unsigned int>(submitted.data.size()),
                                 &submitted.pts_ns, 0) != VPX_CODEC_OK) {
                error = vpx_codec_error_detail(&impl.codec) ? vpx_codec_error_detail(&impl.codec)
                                                            : vpx_codec_error(&impl.codec);
                return MKVC_ERROR_CODEC;
            }
            continue;
        }
        if (!impl.drained) {
            impl.drained = true;
            if (vpx_codec_decode(&impl.codec, nullptr, 0, nullptr, 0) != VPX_CODEC_OK) {
                error = vpx_codec_error(&impl.codec);
                return MKVC_ERROR_CODEC;
            }
            continue;
        }
        return MKVC_END_OF_STREAM;
    }
}

void destroy(CpuVp9Decoder::Impl& impl) noexcept {
    if (impl.codec_initialized) {
        vpx_codec_destroy(&impl.codec);
        impl.codec_initialized = false;
    }
    impl.submitted_packets.clear();
    impl.packet_reader.reset();
}

}  // namespace mkvc::cpu_vp9_decoder_detail
#endif
