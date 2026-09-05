#include "intel_webm_decoder.hpp"

#include <deque>
#include <utility>
#include <vector>

#include "webm_packet_reader.hpp"

namespace mkvc {

struct IntelWebmDecoder::Impl {
#if defined(MKVC_HAS_INTEL_ONEVPL)
    std::unique_ptr<WebmPacketReader> packet_reader;
#endif
    std::unique_ptr<IntelVplDecoder> decoder;
    std::deque<std::unique_ptr<DecodedFrame>> completed;
    std::deque<std::shared_ptr<gpu::GpuFrameCore>> completed_gpu;
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

void enqueue(IntelWebmDecoder::Impl& impl, std::vector<std::unique_ptr<DecodedFrame>>& frames) {
    for (auto& frame : frames) impl.completed.push_back(std::move(frame));
}

void enqueue_gpu(IntelWebmDecoder::Impl& impl,
                 std::vector<std::shared_ptr<gpu::GpuFrameCore>>& frames) {
    for (auto& frame : frames) impl.completed_gpu.push_back(std::move(frame));
}

}  // namespace
#endif

std::unique_ptr<IntelWebmDecoder> IntelWebmDecoder::create(const mkvc_decoder_config& config,
                                                           std::string& error) {
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)config;
    error = "Intel oneVPL backend was not built";
    return nullptr;
#else
    auto result = std::unique_ptr<IntelWebmDecoder>(new IntelWebmDecoder());
    result->impl_->packet_reader =
        WebmPacketReader::open(config.input_path_utf8, config.codec, error);
    if (!result->impl_->packet_reader) return nullptr;
    result->impl_->decoder = IntelVplDecoder::create(config.codec, error, 4, true);
    return result->impl_->decoder ? std::move(result) : nullptr;
#endif
}

mkvc_result IntelWebmDecoder::read_gpu(mkvc_gpu_frame** frame, std::string& error) {
    if (frame != nullptr) *frame = nullptr;
#if !defined(MKVC_HAS_INTEL_ONEVPL)
    (void)frame;
    error = "Intel oneVPL backend was not built";
    return MKVC_ERROR_NOT_SUPPORTED;
#else
    if (frame == nullptr) {
        error = "GPU frame output is null";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    auto& impl = *impl_;
    if (impl.closed) {
        error = "Intel decoder is closed";
        return MKVC_ERROR_INVALID_STATE;
    }
    while (impl.completed_gpu.empty()) {
        if (!impl.demux_eos) {
            EncodedPacket packet;
            const mkvc_result read_result = impl.packet_reader->read(packet, error);
            if (read_result != MKVC_OK && read_result != MKVC_END_OF_STREAM) return read_result;
            impl.demux_eos = read_result == MKVC_END_OF_STREAM;
            if (read_result == MKVC_OK) {
                std::vector<std::shared_ptr<gpu::GpuFrameCore>> completed;
                const mkvc_result decode_result = impl.decoder->decode_gpu(
                    packet.data.data(), packet.data.size(), packet.pts_ns, completed, error);
                if (decode_result != MKVC_OK) return decode_result;
                enqueue_gpu(impl, completed);
            }
            continue;
        }
        if (!impl.drained) {
            std::vector<std::shared_ptr<gpu::GpuFrameCore>> completed;
            const mkvc_result result = impl.decoder->drain_gpu(completed, error);
            if (result != MKVC_OK) return result;
            enqueue_gpu(impl, completed);
            impl.drained = true;
            continue;
        }
        return MKVC_END_OF_STREAM;
    }
    auto core = std::move(impl.completed_gpu.front());
    impl.completed_gpu.pop_front();
    *frame = gpu::make_handle(core);
    return *frame != nullptr ? MKVC_OK : MKVC_ERROR_INTERNAL;
#endif
}

mkvc_result IntelWebmDecoder::read(std::unique_ptr<DecodedFrame>& frame, std::string& error) {
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
            EncodedPacket packet;
            const mkvc_result read_result = impl.packet_reader->read(packet, error);
            if (read_result != MKVC_OK && read_result != MKVC_END_OF_STREAM) return read_result;
            impl.demux_eos = read_result == MKVC_END_OF_STREAM;
            if (read_result == MKVC_OK) {
                std::vector<std::unique_ptr<DecodedFrame>> completed;
                const mkvc_result decode_result = impl.decoder->decode(
                    packet.data.data(), packet.data.size(), packet.pts_ns, completed, error);
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
    impl_->completed_gpu.clear();
    impl_->packet_reader.reset();
#endif
    impl_->closed = true;
    return result;
}

uint32_t IntelWebmDecoder::max_pending_observed() const {
    return std::max(impl_->hardware_pending_peak,
                    impl_->decoder ? impl_->decoder->max_pending_observed() : 0);
}

}  // namespace mkvc
