/**
 * @file webm_muxer.cpp
 * @brief Shared libwebm video-track and container finalization logic.
 */
#include "webm_muxer.hpp"

#include <webm/mkvmuxer/mkvmuxer.h>
#include <webm/mkvmuxer/mkvwriter.h>

#include <memory>
#include <utility>

#include "container_format.hpp"

namespace mkvc {

struct WebmMuxer::Impl {
    mkvmuxer::MkvWriter writer;
    mkvmuxer::Segment segment;
    uint64_t track_number = 0;
    bool writer_open = false;
    bool segment_initialized = false;
    bool finalized = false;
    std::string output_path;
    ContainerFormat container = ContainerFormat::WebM;
};

WebmMuxer::WebmMuxer() : impl_(std::make_unique<Impl>()) {}

WebmMuxer::~WebmMuxer() {
    if (impl_->writer_open) impl_->writer.Close();
}

std::unique_ptr<WebmMuxer> WebmMuxer::create(const char* path, uint32_t codec, uint32_t width,
                                             uint32_t height, uint32_t fps_num, uint32_t fps_den,
                                             std::string& error) {
    if (codec != MKVC_CODEC_VP9 && codec != MKVC_CODEC_AV1) {
        error = "muxer requires VP9 or AV1";
        return nullptr;
    }
    auto result = std::unique_ptr<WebmMuxer>(new WebmMuxer());
    Impl& state = *result->impl_;
    if (!resolve_container_format(path, state.container, error)) return nullptr;
    state.output_path = path;
    if (!state.writer.Open(path)) {
        error = "failed to open output";
        return nullptr;
    }
    state.writer_open = true;
    if (!state.segment.Init(&state.writer)) {
        error = "failed to initialize libwebm";
        return nullptr;
    }
    state.segment_initialized = true;
    state.segment.set_mode(mkvmuxer::Segment::kFile);
    state.track_number =
        state.segment.AddVideoTrack(static_cast<int32_t>(width), static_cast<int32_t>(height), 0);
    auto* track =
        static_cast<mkvmuxer::VideoTrack*>(state.segment.GetTrackByNumber(state.track_number));
    if (state.track_number == 0 || track == nullptr) {
        error = "libwebm failed to create the video track";
        return nullptr;
    }
    track->set_codec_id(codec == MKVC_CODEC_VP9 ? "V_VP9" : "V_AV1");
    if (codec == MKVC_CODEC_AV1) {
        const uint8_t av1_config[4] = {0x81, 19, 0x0c, 0x00};
        if (!track->SetCodecPrivate(av1_config, sizeof(av1_config))) {
            error = "libwebm rejected the AV1 codec configuration";
            return nullptr;
        }
    }
    track->set_frame_rate(static_cast<double>(fps_num) / fps_den);
    track->set_default_duration(static_cast<uint64_t>(fps_den) * 1000000000ULL / fps_num);
    return result;
}

mkvc_result WebmMuxer::add_frame(const uint8_t* data, size_t size, uint64_t timestamp_ns,
                                 uint64_t duration_ns, bool key, std::string& error) {
    if (impl_->finalized || !impl_->segment_initialized || data == nullptr || size == 0) {
        error = "invalid encoded packet or finalized muxer";
        return MKVC_ERROR_INVALID_STATE;
    }
    mkvmuxer::Frame frame;
    if (!frame.Init(data, size)) {
        error = "libwebm failed to copy an encoded packet";
        return MKVC_ERROR_INTERNAL;
    }
    frame.set_track_number(impl_->track_number);
    frame.set_timestamp(timestamp_ns);
    frame.set_duration(duration_ns);
    frame.set_is_key(key);
    if (!impl_->segment.AddGenericFrame(&frame)) {
        error = "libwebm failed to mux an encoded packet";
        return MKVC_ERROR_IO;
    }
    return MKVC_OK;
}

mkvc_result WebmMuxer::finalize(std::string& error) {
    if (impl_->finalized) return MKVC_OK;
    if (impl_->segment_initialized && !impl_->segment.Finalize()) {
        error = "libwebm failed to finalize the output";
        return MKVC_ERROR_IO;
    }
    if (impl_->writer_open) {
        impl_->writer.Close();
        impl_->writer_open = false;
    }
    if (!finalize_container_doc_type(impl_->output_path.c_str(), impl_->container, error)) {
        return MKVC_ERROR_IO;
    }
    impl_->finalized = true;
    return MKVC_OK;
}

}  // namespace mkvc
