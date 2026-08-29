#include "intel_webm_decoder.hpp"

#if defined(MKVC_HAS_INTEL_ONEVPL)
#include <webm/mkvparser/mkvparser.h>
#include <webm/mkvparser/mkvreader.h>
#endif

#include <cstring>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

namespace mkvc {

struct IntelWebmDecoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    struct Packet {
        std::vector<uint8_t> data;
        int64_t pts_ns = 0;
    };
    std::unique_ptr<mkvparser::MkvReader> reader;
    std::unique_ptr<mkvparser::Segment> segment;
    const mkvparser::Cluster* cluster = nullptr;
    const mkvparser::BlockEntry* block_entry = nullptr;
    int block_frame_index = 0;
    long video_track = 0;
#endif
    std::unique_ptr<IntelVplDecoder> decoder;
    std::deque<std::unique_ptr<DecodedFrame>> completed;
    bool demux_eos = false;
    uint32_t hardware_pending_peak = 0;
    bool drained = false;
    bool closed = false;
};

IntelWebmDecoder::IntelWebmDecoder() : impl_(std::make_unique<Impl>()) {}
IntelWebmDecoder::~IntelWebmDecoder() {
    std::string ignored;
    close(ignored);
}

#if defined(MKVC_HAS_INTEL_ONEVPL)
namespace {

bool open_parser(const mkvc_decoder_config& config,
                 IntelWebmDecoder::Impl& impl, std::string& error) {
    impl.reader = std::make_unique<mkvparser::MkvReader>();
    if (impl.reader->Open(config.input_path_utf8) != 0) {
        error = "failed to open Intel Matroska/WebM input";
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
        error = "failed to create Intel libwebm parser";
        return false;
    }
    impl.segment.reset(raw_segment);
    if (impl.segment->Load() < 0) {
        error = "failed to load Intel Matroska/WebM segment";
        return false;
    }
    const char* requested = config.codec == MKVC_CODEC_VP9 ? "V_VP9" : "V_AV1";
    const mkvparser::Tracks* tracks = impl.segment->GetTracks();
    if (tracks == nullptr) {
        error = "input contains no tracks";
        return false;
    }
    for (unsigned long index = 0; index < tracks->GetTracksCount(); ++index) {
        const mkvparser::Track* track = tracks->GetTrackByIndex(index);
        if (track != nullptr && track->GetType() == mkvparser::Track::kVideo &&
            track->GetCodecId() != nullptr &&
            std::strcmp(track->GetCodecId(), requested) == 0) {
            impl.video_track = track->GetNumber();
            break;
        }
    }
    if (impl.video_track == 0) {
        error = "input has no requested VP9/AV1 video track";
        return false;
    }
    impl.cluster = impl.segment->GetFirst();
    return true;
}

mkvc_result read_next_packet(IntelWebmDecoder::Impl& impl,
                             IntelWebmDecoder::Impl::Packet& packet,
                             std::string& error) {
    constexpr uint64_t kMaxPacketBytes = 256ULL * 1024 * 1024;
    while (impl.cluster != nullptr && !impl.cluster->EOS()) {
        if (impl.block_entry == nullptr) {
            if (impl.cluster->GetFirst(impl.block_entry) < 0) {
                error = "failed to read first Intel cluster block";
                return MKVC_ERROR_IO;
            }
            impl.block_frame_index = 0;
        }
        while (impl.block_entry != nullptr && !impl.block_entry->EOS()) {
            const mkvparser::Block* block = impl.block_entry->GetBlock();
            if (block != nullptr && block->GetTrackNumber() == impl.video_track) {
                while (impl.block_frame_index < block->GetFrameCount()) {
                    const auto& source = block->GetFrame(impl.block_frame_index++);
                    if (source.len <= 0 ||
                        static_cast<uint64_t>(source.len) > kMaxPacketBytes ||
                        static_cast<uint64_t>(source.len) >
                            std::numeric_limits<size_t>::max()) {
                        error = "invalid Intel encoded frame size";
                        return MKVC_ERROR_IO;
                    }
                    packet.data.resize(static_cast<size_t>(source.len));
                    if (source.Read(impl.reader.get(), packet.data.data()) != 0) {
                        error = "failed to read Intel encoded frame";
                        return MKVC_ERROR_IO;
                    }
                    packet.pts_ns = block->GetTime(impl.cluster);
                    return MKVC_OK;
                }
            }
            const mkvparser::BlockEntry* next = nullptr;
            if (impl.cluster->GetNext(impl.block_entry, next) < 0) {
                error = "failed to advance Intel cluster block";
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

void enqueue(IntelWebmDecoder::Impl& impl,
             std::vector<std::unique_ptr<DecodedFrame>>& frames) {
    for (auto& frame : frames) impl.completed.push_back(std::move(frame));
}

}  // namespace
#endif

std::unique_ptr<IntelWebmDecoder> IntelWebmDecoder::create(
    const mkvc_decoder_config& config, std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)config;
    error = "Intel oneVPL backend was not built";
    return nullptr;
#else
    auto result = std::unique_ptr<IntelWebmDecoder>(new IntelWebmDecoder());
    if (!open_parser(config, *result->impl_, error)) return nullptr;
    result->impl_->decoder = IntelVplDecoder::create(config.codec, error);
    return result->impl_->decoder ? std::move(result) : nullptr;
#endif
}

mkvc_result IntelWebmDecoder::read(std::unique_ptr<DecodedFrame>& frame,
                                   std::string& error) {
    frame.reset();
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    auto& impl = *impl_;
    if (impl.closed) {
        error = "Intel decoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    while (impl.completed.empty()) {
        if (!impl.demux_eos) {
            Impl::Packet packet;
            const mkvc_result read_result = read_next_packet(impl, packet, error);
            if (read_result != MKVC_OK && read_result != MKVC_END_OF_STREAM)
                return read_result;
            if (read_result == MKVC_OK) {
                std::vector<std::unique_ptr<DecodedFrame>> completed;
                const mkvc_result decode_result = impl.decoder->decode(
                    packet.data.data(), packet.data.size(), packet.pts_ns,
                    completed, error);
                if (decode_result != MKVC_OK) return decode_result;
                enqueue(impl, completed);
            }
            continue;
        }
        if (!impl.drained) {
            std::vector<std::unique_ptr<DecodedFrame>> completed;
            const mkvc_result result = impl.decoder->drain(completed, error);
            if (result != MKVC_OK) return result;
            enqueue(impl, completed);
            impl.drained = true;
            continue;
        }
        return MKVC_END_OF_STREAM;
    }
    frame = std::move(impl.completed.front());
    impl.completed.pop_front();
    return MKVC_OK;
#endif
}

mkvc_result IntelWebmDecoder::close(std::string& error) {
    if (impl_->closed) return MKVC_OK;
    mkvc_result result = MKVC_OK;
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)error;
#else
    if (impl_->decoder) {
        impl_->hardware_pending_peak = impl_->decoder->max_pending_observed();
        result = impl_->decoder->close(error);
    }
    impl_->decoder.reset();
    impl_->completed.clear();
    impl_->segment.reset();
    impl_->reader.reset();
#endif
    impl_->closed = true;
    return result;
}

uint32_t IntelWebmDecoder::max_pending_observed() const {
    return std::max(impl_->hardware_pending_peak,
                    impl_->decoder ? impl_->decoder->max_pending_observed() : 0);
}

}  // namespace mkvc
