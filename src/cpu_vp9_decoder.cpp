#include "cpu_vp9_decoder.hpp"

#if defined(MKVC_HAS_CPU_VP9)
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#include <webm/mkvparser/mkvparser.h>
#include <webm/mkvparser/mkvreader.h>
#endif

#include <cstring>
#include <limits>
#include <utility>

namespace mkvc {

struct CpuVp9Decoder::Impl {
#if defined(MKVC_HAS_CPU_VP9)
    struct Packet {
        std::vector<uint8_t> data;
        int64_t pts_ns = 0;
    };
    vpx_codec_ctx_t codec{};
    bool codec_initialized = false;
    vpx_codec_iter_t iterator = nullptr;
    std::vector<Packet> packets;
#endif
    size_t next_packet = 0;
    bool drained = false;
    bool closed = false;
};

CpuVp9Decoder::CpuVp9Decoder() : impl_(std::make_unique<Impl>()) {}

CpuVp9Decoder::~CpuVp9Decoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_CPU_VP9)
namespace {

bool load_packets(const char* path, std::vector<CpuVp9Decoder::Impl::Packet>& packets,
                  std::string& error) {
    constexpr uint64_t max_packet_bytes = 256ULL * 1024 * 1024;
    constexpr uint64_t max_total_packet_bytes = 1024ULL * 1024 * 1024;
    uint64_t total_packet_bytes = 0;
    mkvparser::MkvReader reader;
    if (reader.Open(path) != 0) {
        error = "failed to open Matroska/WebM input";
        return false;
    }
    long long position = 0;
    mkvparser::EBMLHeader header;
    if (header.Parse(&reader, position) != 0) {
        error = "invalid EBML header";
        return false;
    }
    mkvparser::Segment* raw_segment = nullptr;
    if (mkvparser::Segment::CreateInstance(&reader, position, raw_segment) != 0 ||
        raw_segment == nullptr) {
        error = "failed to create libwebm parser";
        return false;
    }
    std::unique_ptr<mkvparser::Segment> segment(raw_segment);
    if (segment->Load() < 0) {
        error = "failed to load Matroska/WebM segment";
        return false;
    }

    const mkvparser::Tracks* tracks = segment->GetTracks();
    if (tracks == nullptr) {
        error = "input contains no tracks";
        return false;
    }
    long video_track = 0;
    for (unsigned long index = 0; index < tracks->GetTracksCount(); ++index) {
        const mkvparser::Track* track = tracks->GetTrackByIndex(index);
        if (track != nullptr && track->GetType() == mkvparser::Track::kVideo &&
            track->GetCodecId() != nullptr &&
            std::strcmp(track->GetCodecId(), "V_VP9") == 0) {
            video_track = track->GetNumber();
            break;
        }
    }
    if (video_track == 0) {
        error = "input has no VP9 video track";
        return false;
    }

    for (const mkvparser::Cluster* cluster = segment->GetFirst();
         cluster != nullptr && !cluster->EOS(); cluster = segment->GetNext(cluster)) {
        const mkvparser::BlockEntry* entry = nullptr;
        if (cluster->GetFirst(entry) < 0) {
            error = "failed to read first cluster block";
            return false;
        }
        while (entry != nullptr && !entry->EOS()) {
            const mkvparser::Block* block = entry->GetBlock();
            if (block != nullptr && block->GetTrackNumber() == video_track) {
                const int64_t pts_ns = block->GetTime(cluster);
                for (int index = 0; index < block->GetFrameCount(); ++index) {
                    const auto& source = block->GetFrame(index);
                    if (source.len <= 0 ||
                        static_cast<uint64_t>(source.len) > max_packet_bytes ||
                        static_cast<uint64_t>(source.len) >
                            std::numeric_limits<size_t>::max() ||
                        total_packet_bytes >
                            max_total_packet_bytes - static_cast<uint64_t>(source.len)) {
                        error = "invalid encoded frame size";
                        return false;
                    }
                    CpuVp9Decoder::Impl::Packet packet;
                    packet.data.resize(static_cast<size_t>(source.len));
                    if (source.Read(&reader, packet.data.data()) != 0) {
                        error = "failed to read encoded VP9 frame";
                        return false;
                    }
                    packet.pts_ns = pts_ns;
                    total_packet_bytes += static_cast<uint64_t>(source.len);
                    packets.push_back(std::move(packet));
                }
            }
            if (cluster->GetNext(entry, entry) < 0) {
                error = "failed to advance cluster block";
                return false;
            }
        }
    }
    if (packets.empty()) {
        error = "VP9 track contains no frames";
        return false;
    }
    return true;
}

std::unique_ptr<DecodedFrame> copy_image(const vpx_image_t& image,
                                         int64_t pts_ns,
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
    frame->strides = {static_cast<int32_t>(image.d_w),
                      static_cast<int32_t>(image.d_w / 2),
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
#endif

std::unique_ptr<CpuVp9Decoder> CpuVp9Decoder::create(
    const mkvc_decoder_config& config, std::string& error) {
#if !defined(MKVC_HAS_CPU_VP9)
    (void)config;
    error = "CPU VP9 backend was not built";
    return nullptr;
#else
    auto decoder = std::unique_ptr<CpuVp9Decoder>(new CpuVp9Decoder());
    if (!load_packets(config.input_path_utf8, decoder->impl_->packets, error)) {
        return nullptr;
    }
    vpx_codec_dec_cfg_t codec_config{};
    codec_config.threads = config.threads;
    if (vpx_codec_dec_init(&decoder->impl_->codec, vpx_codec_vp9_dx(),
                           &codec_config, 0) != VPX_CODEC_OK) {
        error = vpx_codec_error(&decoder->impl_->codec);
        return nullptr;
    }
    decoder->impl_->codec_initialized = true;
    return decoder;
#endif
}

mkvc_result CpuVp9Decoder::read(std::unique_ptr<DecodedFrame>& frame,
                                std::string& error) {
    frame.reset();
#if !defined(MKVC_HAS_CPU_VP9)
    error = "CPU VP9 backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) {
        error = "decoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    while (true) {
        if (vpx_image_t* image = vpx_codec_get_frame(&impl.codec, &impl.iterator)) {
            const int64_t pts_ns = image->user_priv == nullptr
                ? 0
                : *static_cast<const int64_t*>(image->user_priv);
            frame = copy_image(*image, pts_ns, error);
            return frame ? MKVC_OK : MKVC_ERROR_NOT_SUPPORTED;
        }
        if (impl.next_packet < impl.packets.size()) {
            auto& packet = impl.packets[impl.next_packet++];
            if (vpx_codec_decode(&impl.codec, packet.data.data(),
                                 static_cast<unsigned int>(packet.data.size()),
                                 &packet.pts_ns, 0) != VPX_CODEC_OK) {
                error = vpx_codec_error_detail(&impl.codec)
                            ? vpx_codec_error_detail(&impl.codec)
                            : vpx_codec_error(&impl.codec);
                return MKVC_ERROR_CODEC;
            }
            continue;
        }
        if (!impl.drained) {
            impl.drained = true;
            if (vpx_codec_decode(&impl.codec, nullptr, 0, nullptr, 0) !=
                VPX_CODEC_OK) {
                error = vpx_codec_error(&impl.codec);
                return MKVC_ERROR_CODEC;
            }
            continue;
        }
        return MKVC_END_OF_STREAM;
    }
#endif
}

mkvc_result CpuVp9Decoder::close(std::string& error) {
    (void)error;
    if (impl_->closed) {
        return MKVC_OK;
    }
#if defined(MKVC_HAS_CPU_VP9)
    if (impl_->codec_initialized) {
        vpx_codec_destroy(&impl_->codec);
        impl_->codec_initialized = false;
    }
    impl_->packets.clear();
#endif
    impl_->closed = true;
    return MKVC_OK;
}

}  // namespace mkvc
