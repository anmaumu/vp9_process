#include "input_video_probe.hpp"

#include <webm/mkvparser/mkvparser.h>
#include <webm/mkvparser/mkvreader.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>

#include "container_format.hpp"

namespace mkvc {
namespace {

void set_frame_rate(const mkvparser::VideoTrack& track, mkvc_video_info& info) {
    const double rate = track.GetFrameRate();
    if (std::isfinite(rate) && rate > 0.0) {
        constexpr uint64_t kScale = 1000000;
        const uint64_t numerator = static_cast<uint64_t>(std::llround(rate * kScale));
        const uint64_t divisor = std::gcd<uint64_t>(numerator, kScale);
        if (numerator != 0 && numerator / divisor <= std::numeric_limits<uint32_t>::max()) {
            info.fps_num = static_cast<uint32_t>(numerator / divisor);
            info.fps_den = static_cast<uint32_t>(kScale / divisor);
            info.fps_known = 1;
            return;
        }
    }
    const uint64_t duration = track.GetDefaultDuration();
    if (duration != 0) {
        const uint64_t divisor = std::gcd<uint64_t>(1000000000ULL, duration);
        const uint64_t numerator = 1000000000ULL / divisor;
        const uint64_t denominator = duration / divisor;
        if (numerator <= std::numeric_limits<uint32_t>::max() &&
            denominator <= std::numeric_limits<uint32_t>::max()) {
            info.fps_num = static_cast<uint32_t>(numerator);
            info.fps_den = static_cast<uint32_t>(denominator);
            info.fps_known = 1;
            return;
        }
    }
}

mkvc_result count_frames(mkvparser::Segment& segment, long track_number, uint64_t& count,
                         std::string& error) {
    count = 0;
    for (const mkvparser::Cluster* cluster = segment.GetFirst();
         cluster != nullptr && !cluster->EOS(); cluster = segment.GetNext(cluster)) {
        const mkvparser::BlockEntry* entry = nullptr;
        if (cluster->GetFirst(entry) < 0) {
            error = "failed to inspect first cluster block";
            return MKVC_ERROR_IO;
        }
        while (entry != nullptr && !entry->EOS()) {
            const mkvparser::Block* block = entry->GetBlock();
            if (block != nullptr && block->GetTrackNumber() == track_number) {
                const int frames = block->GetFrameCount();
                if (frames < 0 ||
                    count > std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(frames)) {
                    error = "invalid video frame count";
                    return MKVC_ERROR_IO;
                }
                count += static_cast<uint64_t>(frames);
            }
            const mkvparser::BlockEntry* next = nullptr;
            if (cluster->GetNext(entry, next) < 0) {
                error = "failed to inspect cluster blocks";
                return MKVC_ERROR_IO;
            }
            entry = next;
        }
    }
    return MKVC_OK;
}

}  // namespace

mkvc_result probe_input_video(const char* path, mkvc_video_info& info, std::string& error) {
    info = {};
    info.struct_size = sizeof(info);
    info.struct_version = 1;
    ContainerFormat format;
    if (path == nullptr || !resolve_container_format(path, format, error) ||
        !validate_container_doc_type(path, format, error)) {
        return MKVC_ERROR_IO;
    }
    auto reader = std::make_unique<mkvparser::MkvReader>();
    if (reader->Open(path) != 0) {
        error = "failed to open Matroska/WebM input";
        return MKVC_ERROR_IO;
    }
    long long position = 0;
    mkvparser::EBMLHeader header;
    if (header.Parse(reader.get(), position) != 0) {
        error = "invalid EBML header";
        return MKVC_ERROR_IO;
    }
    mkvparser::Segment* raw_segment = nullptr;
    if (mkvparser::Segment::CreateInstance(reader.get(), position, raw_segment) != 0 ||
        raw_segment == nullptr) {
        error = "failed to create libwebm parser";
        return MKVC_ERROR_IO;
    }
    std::unique_ptr<mkvparser::Segment> segment(raw_segment);
    if (segment->Load() < 0) {
        error = "failed to load Matroska/WebM segment";
        return MKVC_ERROR_IO;
    }
    const mkvparser::Tracks* tracks = segment->GetTracks();
    if (tracks == nullptr) {
        error = "input contains no tracks";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    const mkvparser::VideoTrack* selected = nullptr;
    for (unsigned long index = 0; index < tracks->GetTracksCount(); ++index) {
        const mkvparser::Track* track = tracks->GetTrackByIndex(index);
        if (track == nullptr || track->GetType() != mkvparser::Track::kVideo ||
            track->GetCodecId() == nullptr) {
            continue;
        }
        if (std::strcmp(track->GetCodecId(), "V_VP9") == 0) info.codec = MKVC_CODEC_VP9;
        if (std::strcmp(track->GetCodecId(), "V_AV1") == 0) info.codec = MKVC_CODEC_AV1;
        if (info.codec != MKVC_CODEC_AUTO) {
            selected = static_cast<const mkvparser::VideoTrack*>(track);
            break;
        }
    }
    if (selected == nullptr) {
        error = "input has no supported VP9/AV1 video track";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    const long long width = selected->GetWidth();
    const long long height = selected->GetHeight();
    if (width <= 0 || height <= 0 || width > std::numeric_limits<uint32_t>::max() ||
        height > std::numeric_limits<uint32_t>::max()) {
        error = "input video dimensions are invalid";
        return MKVC_ERROR_IO;
    }
    info.width = static_cast<uint32_t>(width);
    info.height = static_cast<uint32_t>(height);
    set_frame_rate(*selected, info);
    const long long duration = segment->GetDuration();
    if (duration >= 0) {
        info.duration_ns = duration;
        info.duration_known = 1;
    }
    const mkvc_result count_result =
        count_frames(*segment, selected->GetNumber(), info.frame_count, error);
    if (count_result != MKVC_OK) return count_result;
    info.frame_count_known = 1;
    return MKVC_OK;
}

}  // namespace mkvc
