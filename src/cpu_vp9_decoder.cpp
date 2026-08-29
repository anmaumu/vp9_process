#include "cpu_vp9_decoder.hpp"

#if defined(MKVC_HAS_CPU_VP9)
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#include <webm/mkvparser/mkvparser.h>
#include <webm/mkvparser/mkvreader.h>
#endif

#include <algorithm>
#include <cstring>
#include <list>
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
    std::unique_ptr<mkvparser::MkvReader> reader;
    std::unique_ptr<mkvparser::Segment> segment;
    const mkvparser::Cluster* cluster = nullptr;
    const mkvparser::BlockEntry* block_entry = nullptr;
    int block_frame_index = 0;
    long video_track = 0;
    std::list<Packet> submitted_packets;
#endif
    bool demux_eos = false;
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

bool open_parser(const char* path, CpuVp9Decoder::Impl& impl,
                 std::string& error) {
    impl.reader = std::make_unique<mkvparser::MkvReader>();
    if (impl.reader->Open(path) != 0) {
        error = "failed to open Matroska/WebM input";
        return false;
    }
    long long position = 0;
    mkvparser::EBMLHeader header;
    if (header.Parse(impl.reader.get(), position) != 0) {
        error = "invalid EBML header";
        return false;
    }
    mkvparser::Segment* raw_segment = nullptr;
    if (mkvparser::Segment::CreateInstance(impl.reader.get(), position,
                                           raw_segment) != 0 ||
        raw_segment == nullptr) {
        error = "failed to create libwebm parser";
        return false;
    }
    impl.segment.reset(raw_segment);
    if (impl.segment->Load() < 0) {
        error = "failed to load Matroska/WebM segment";
        return false;
    }

    const mkvparser::Tracks* tracks = impl.segment->GetTracks();
    if (tracks == nullptr) {
        error = "input contains no tracks";
        return false;
    }
    for (unsigned long index = 0; index < tracks->GetTracksCount(); ++index) {
        const mkvparser::Track* track = tracks->GetTrackByIndex(index);
        if (track != nullptr && track->GetType() == mkvparser::Track::kVideo &&
            track->GetCodecId() != nullptr &&
            std::strcmp(track->GetCodecId(), "V_VP9") == 0) {
            impl.video_track = track->GetNumber();
            break;
        }
    }
    if (impl.video_track == 0) {
        error = "input has no VP9 video track";
        return false;
    }

    impl.cluster = impl.segment->GetFirst();
    return true;
}

mkvc_result read_next_packet(CpuVp9Decoder::Impl& impl,
                             CpuVp9Decoder::Impl::Packet& packet,
                             std::string& error) {
    constexpr uint64_t max_packet_bytes = 256ULL * 1024 * 1024;
    while (impl.cluster != nullptr && !impl.cluster->EOS()) {
        if (impl.block_entry == nullptr) {
            if (impl.cluster->GetFirst(impl.block_entry) < 0) {
                error = "failed to read first cluster block";
                return MKVC_ERROR_IO;
            }
            impl.block_frame_index = 0;
        }
        while (impl.block_entry != nullptr && !impl.block_entry->EOS()) {
            const mkvparser::Block* block = impl.block_entry->GetBlock();
            if (block != nullptr &&
                block->GetTrackNumber() == impl.video_track) {
                while (impl.block_frame_index < block->GetFrameCount()) {
                    const auto& source =
                        block->GetFrame(impl.block_frame_index++);
                    if (source.len <= 0 ||
                        static_cast<uint64_t>(source.len) > max_packet_bytes ||
                        static_cast<uint64_t>(source.len) >
                            std::numeric_limits<size_t>::max()) {
                        error = "invalid encoded frame size";
                        return MKVC_ERROR_IO;
                    }
                    packet.data.resize(static_cast<size_t>(source.len));
                    if (source.Read(impl.reader.get(), packet.data.data()) != 0) {
                        error = "failed to read encoded VP9 frame";
                        return MKVC_ERROR_IO;
                    }
                    packet.pts_ns = block->GetTime(impl.cluster);
                    return MKVC_OK;
                }
            }
            const mkvparser::BlockEntry* next = nullptr;
            if (impl.cluster->GetNext(impl.block_entry, next) < 0) {
                error = "failed to advance cluster block";
                return MKVC_ERROR_IO;
            }
            impl.block_entry = next;
            impl.block_frame_index = 0;
        }
        impl.cluster = impl.segment->GetNext(impl.cluster);
        impl.block_entry = nullptr;
    }
    impl.demux_eos = true;
    return MKVC_END_OF_STREAM;
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
    if (!open_parser(config.input_path_utf8, *decoder->impl_, error)) {
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
            const auto* pts_pointer =
                static_cast<const int64_t*>(image->user_priv);
            const int64_t pts_ns = pts_pointer == nullptr ? 0 : *pts_pointer;
            frame = copy_image(*image, pts_ns, error);
            if (pts_pointer != nullptr) {
                const auto submitted = std::find_if(
                    impl.submitted_packets.begin(), impl.submitted_packets.end(),
                    [pts_pointer](const Impl::Packet& packet) {
                        return &packet.pts_ns == pts_pointer;
                    });
                if (submitted != impl.submitted_packets.end()) {
                    impl.submitted_packets.erase(submitted);
                }
            }
            return frame ? MKVC_OK : MKVC_ERROR_NOT_SUPPORTED;
        }
        if (!impl.demux_eos) {
            Impl::Packet packet;
            const mkvc_result demux_result =
                read_next_packet(impl, packet, error);
            if (demux_result != MKVC_OK) {
                if (demux_result != MKVC_END_OF_STREAM) {
                    return demux_result;
                }
                continue;
            }
            impl.submitted_packets.push_back(std::move(packet));
            auto& submitted = impl.submitted_packets.back();
            if (vpx_codec_decode(&impl.codec, submitted.data.data(),
                                 static_cast<unsigned int>(submitted.data.size()),
                                 &submitted.pts_ns, 0) != VPX_CODEC_OK) {
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
    impl_->submitted_packets.clear();
    impl_->segment.reset();
    if (impl_->reader) {
        impl_->reader->Close();
        impl_->reader.reset();
    }
#endif
    impl_->closed = true;
    return MKVC_OK;
}

}  // namespace mkvc
