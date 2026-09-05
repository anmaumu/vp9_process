/**
 * @file webm_packet_reader.cpp
 * @brief Shared libwebm cursor and compressed-packet extraction.
 */
#include "webm_packet_reader.hpp"

#include <webm/mkvparser/mkvparser.h>
#include <webm/mkvparser/mkvreader.h>

#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include "container_format.hpp"

namespace mkvc {

struct WebmPacketReader::Impl {
    std::unique_ptr<mkvparser::MkvReader> reader;
    std::unique_ptr<mkvparser::Segment> segment;
    const mkvparser::Cluster* cluster = nullptr;
    const mkvparser::BlockEntry* block_entry = nullptr;
    int block_frame_index = 0;
    long video_track = 0;
};

WebmPacketReader::WebmPacketReader() : impl_(std::make_unique<Impl>()) {}
WebmPacketReader::~WebmPacketReader() = default;

std::unique_ptr<WebmPacketReader> WebmPacketReader::open(const char* path, uint32_t codec,
                                                         std::string& error) {
    ContainerFormat format;
    if (!resolve_container_format(path, format, error) ||
        !validate_container_doc_type(path, format, error)) {
        return nullptr;
    }
    if (codec != MKVC_CODEC_VP9 && codec != MKVC_CODEC_AV1) {
        error = "packet reader requires VP9 or AV1";
        return nullptr;
    }

    auto result = std::unique_ptr<WebmPacketReader>(new WebmPacketReader());
    Impl& state = *result->impl_;
    state.reader = std::make_unique<mkvparser::MkvReader>();
    if (state.reader->Open(path) != 0) {
        error = "failed to open Matroska/WebM input";
        return nullptr;
    }
    long long position = 0;
    mkvparser::EBMLHeader header;
    if (header.Parse(state.reader.get(), position) != 0) {
        error = "invalid EBML header";
        return nullptr;
    }
    mkvparser::Segment* raw_segment = nullptr;
    if (mkvparser::Segment::CreateInstance(state.reader.get(), position, raw_segment) != 0 ||
        raw_segment == nullptr) {
        error = "failed to create libwebm parser";
        return nullptr;
    }
    state.segment.reset(raw_segment);
    if (state.segment->Load() < 0) {
        error = "failed to load Matroska/WebM segment";
        return nullptr;
    }

    const char* requested = codec == MKVC_CODEC_VP9 ? "V_VP9" : "V_AV1";
    const mkvparser::Tracks* tracks = state.segment->GetTracks();
    if (tracks == nullptr) {
        error = "input contains no tracks";
        return nullptr;
    }
    for (unsigned long index = 0; index < tracks->GetTracksCount(); ++index) {
        const mkvparser::Track* track = tracks->GetTrackByIndex(index);
        if (track != nullptr && track->GetType() == mkvparser::Track::kVideo &&
            track->GetCodecId() != nullptr && std::strcmp(track->GetCodecId(), requested) == 0) {
            state.video_track = track->GetNumber();
            break;
        }
    }
    if (state.video_track == 0) {
        error = "input has no requested VP9/AV1 video track";
        return nullptr;
    }
    state.cluster = state.segment->GetFirst();
    return result;
}

mkvc_result WebmPacketReader::read(EncodedPacket& packet, std::string& error) {
    constexpr uint64_t kMaxPacketBytes = 256ULL * 1024 * 1024;
    Impl& state = *impl_;
    while (state.cluster != nullptr && !state.cluster->EOS()) {
        if (state.block_entry == nullptr) {
            if (state.cluster->GetFirst(state.block_entry) < 0) {
                error = "failed to read first cluster block";
                return MKVC_ERROR_IO;
            }
            state.block_frame_index = 0;
        }
        while (state.block_entry != nullptr && !state.block_entry->EOS()) {
            const mkvparser::Block* block = state.block_entry->GetBlock();
            if (block != nullptr && block->GetTrackNumber() == state.video_track) {
                while (state.block_frame_index < block->GetFrameCount()) {
                    const auto& source = block->GetFrame(state.block_frame_index++);
                    if (source.len <= 0 || static_cast<uint64_t>(source.len) > kMaxPacketBytes ||
                        static_cast<uint64_t>(source.len) > std::numeric_limits<size_t>::max()) {
                        error = "invalid encoded frame size";
                        return MKVC_ERROR_IO;
                    }
                    packet.data.resize(static_cast<size_t>(source.len));
                    if (source.Read(state.reader.get(), packet.data.data()) != 0) {
                        error = "failed to read encoded frame";
                        return MKVC_ERROR_IO;
                    }
                    packet.pts_ns = block->GetTime(state.cluster);
                    return MKVC_OK;
                }
            }
            const mkvparser::BlockEntry* next = nullptr;
            if (state.cluster->GetNext(state.block_entry, next) < 0) {
                error = "failed to advance cluster block";
                return MKVC_ERROR_IO;
            }
            state.block_entry = next;
            state.block_frame_index = 0;
        }
        state.cluster = state.segment->GetNext(state.cluster);
        state.block_entry = nullptr;
    }
    return MKVC_END_OF_STREAM;
}

}  // namespace mkvc
