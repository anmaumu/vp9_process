#include "mkvcodec/mkvc.h"

#include "backend_registry.hpp"
#include "cpu_vp9_decoder.hpp"
#include "cpu_av1_decoder.hpp"
#include "encoder_session.hpp"
#include "frame_conversion.hpp"
#include "intel_webm_decoder.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <exception>
#include <memory>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

struct mkvc_encoder {
    std::unique_ptr<mkvc::EncoderSession> implementation;
};

struct mkvc_decoder {
    std::unique_ptr<mkvc::CpuVp9Decoder> implementation;
    std::unique_ptr<mkvc::CpuAv1Decoder> av1_implementation;
    std::unique_ptr<mkvc::IntelWebmDecoder> intel_implementation;
    mutable std::mutex mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;
    std::deque<std::unique_ptr<mkvc::DecodedFrame>> queue;
    std::thread worker;
    size_t capacity = 0;
    bool stop_requested = false;
    bool worker_finished = false;
    mkvc_result worker_result = MKVC_OK;
    std::string worker_error;
    uint64_t accepted_frames = 0;
    uint64_t completed_frames = 0;
    uint64_t queue_wait_ns = 0;
    uint64_t backend_time_ns = 0;
    uint32_t peak_queue_depth = 0;
    uint32_t hardware_pending_peak = 0;
};

struct mkvc_frame {
    std::atomic<uint32_t> references{1};
    std::unique_ptr<mkvc::DecodedFrame> implementation;
};

namespace {
thread_local std::string last_error;

mkvc_result fail(mkvc_result result, std::string message) {
    last_error = std::move(message);
    return result;
}

uint64_t elapsed_ns(std::chrono::steady_clock::time_point started) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
}

mkvc_result decoder_read_backend(
    mkvc_decoder* decoder, std::unique_ptr<mkvc::DecodedFrame>& frame,
    std::string& error) {
    if (decoder->intel_implementation)
        return decoder->intel_implementation->read(frame, error);
    return decoder->implementation ? decoder->implementation->read(frame, error)
                                   : decoder->av1_implementation->read(frame, error);
}

mkvc_result decoder_close_backend(mkvc_decoder* decoder,
                                  std::string& error) {
    if (decoder->intel_implementation)
        return decoder->intel_implementation->close(error);
    return decoder->implementation ? decoder->implementation->close(error)
                                   : decoder->av1_implementation->close(error);
}

void decoder_worker(mkvc_decoder* decoder) noexcept {
    try {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(decoder->mutex);
                decoder->not_full.wait(lock, [decoder] {
                    return decoder->stop_requested ||
                           decoder->queue.size() < decoder->capacity;
                });
                if (decoder->stop_requested) {
                    return;
                }
            }

            std::unique_ptr<mkvc::DecodedFrame> frame;
            std::string error;
            const auto backend_started = std::chrono::steady_clock::now();
            const mkvc_result result =
                decoder_read_backend(decoder, frame, error);
            const uint64_t backend_elapsed = elapsed_ns(backend_started);
            const uint32_t hardware_pending = decoder->intel_implementation
                ? decoder->intel_implementation->max_pending_observed() : 0;
            std::lock_guard<std::mutex> lock(decoder->mutex);
            decoder->backend_time_ns += backend_elapsed;
            decoder->hardware_pending_peak = std::max(
                decoder->hardware_pending_peak, hardware_pending);
            if (decoder->stop_requested) {
                return;
            }
            if (result == MKVC_OK) {
                decoder->queue.push_back(std::move(frame));
                ++decoder->accepted_frames;
                decoder->peak_queue_depth = std::max<uint32_t>(
                    decoder->peak_queue_depth,
                    static_cast<uint32_t>(decoder->queue.size()));
                decoder->not_empty.notify_one();
                continue;
            }
            decoder->worker_result = result;
            decoder->worker_error = std::move(error);
            decoder->worker_finished = true;
            decoder->not_empty.notify_all();
            return;
        }
    } catch (const std::exception& exception) {
        std::lock_guard<std::mutex> lock(decoder->mutex);
        decoder->worker_result = MKVC_ERROR_INTERNAL;
        decoder->worker_error = exception.what();
        decoder->worker_finished = true;
        decoder->not_empty.notify_all();
    } catch (...) {
        std::lock_guard<std::mutex> lock(decoder->mutex);
        decoder->worker_result = MKVC_ERROR_INTERNAL;
        decoder->worker_error = "unknown decoder prefetch failure";
        decoder->worker_finished = true;
        decoder->not_empty.notify_all();
    }
}

void stop_decoder_worker(mkvc_decoder* decoder) noexcept {
    if (decoder == nullptr || decoder->capacity == 0) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(decoder->mutex);
        decoder->stop_requested = true;
        decoder->not_empty.notify_all();
        decoder->not_full.notify_all();
    }
    if (decoder->worker.joinable()) {
        decoder->worker.join();
    }
    std::lock_guard<std::mutex> lock(decoder->mutex);
    decoder->queue.clear();
}
}  // namespace

extern "C" {

mkvc_result mkvc_get_version(mkvc_version* out_version) {
    if (out_version == nullptr || out_version->struct_size < sizeof(mkvc_version)) {
        return MKVC_ERROR_INVALID_ARGUMENT;
    }

    out_version->abi_version = MKVC_ABI_VERSION;
    out_version->major = 0;
    out_version->minor = 1;
    out_version->patch = 0;
    return MKVC_OK;
}

mkvc_result mkvc_get_backend_capabilities(
    mkvc_backend_capability* capabilities,
    size_t* inout_count) {
    if (inout_count == nullptr) {
        return MKVC_ERROR_INVALID_ARGUMENT;
    }

    const auto& available = mkvc::backend_capabilities();
    const size_t required = available.size();
    if (capabilities == nullptr) {
        *inout_count = required;
        return MKVC_OK;
    }
    if (*inout_count < required) {
        *inout_count = required;
        return MKVC_ERROR_BUFFER_TOO_SMALL;
    }

    std::copy(available.begin(), available.end(), capabilities);
    *inout_count = required;
    return MKVC_OK;
}

const char* mkvc_result_string(mkvc_result result) {
    switch (result) {
        case MKVC_OK: return "ok";
        case MKVC_ERROR_INVALID_ARGUMENT: return "invalid argument";
        case MKVC_ERROR_BUFFER_TOO_SMALL: return "buffer too small";
        case MKVC_ERROR_NOT_SUPPORTED: return "not supported";
        case MKVC_ERROR_INTERNAL: return "internal error";
        case MKVC_ERROR_INVALID_STATE: return "invalid state";
        case MKVC_ERROR_IO: return "I/O error";
        case MKVC_ERROR_CODEC: return "codec error";
        case MKVC_END_OF_STREAM: return "end of stream";
        case MKVC_WOULD_BLOCK: return "would block";
        default: return "unknown result";
    }
}

mkvc_result mkvc_encoder_create(const mkvc_encoder_config* config,
                                mkvc_encoder** out_encoder) {
    last_error.clear();
    if (out_encoder != nullptr) {
        *out_encoder = nullptr;
    }
    if (config == nullptr || out_encoder == nullptr ||
        config->struct_size < sizeof(mkvc_encoder_config) ||
        config->struct_version != 1 || config->output_path_utf8 == nullptr ||
        config->output_path_utf8[0] == '\0' ||
        (config->codec != MKVC_CODEC_VP9 && config->codec != MKVC_CODEC_AV1) ||
        (config->backend != MKVC_BACKEND_CPU &&
         config->backend != MKVC_BACKEND_INTEL) || config->width == 0 ||
        config->height == 0 || (config->width & 1u) != 0 ||
        (config->height & 1u) != 0 || config->fps_num == 0 ||
        config->fps_den == 0 || config->quality > 63 ||
        config->width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max() / 4) ||
        config->height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder config");
    }
    try {
        std::string error;
        auto implementation = mkvc::EncoderSession::create(*config, error);
        if (!implementation) {
            return fail(MKVC_ERROR_CODEC, std::move(error));
        }
        auto handle = std::make_unique<mkvc_encoder>();
        handle->implementation = std::move(implementation);
        *out_encoder = handle.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown encoder creation failure");
    }
}

mkvc_result mkvc_encoder_write_frame(mkvc_encoder* encoder,
                                     const mkvc_frame_view* frame) {
    last_error.clear();
    if (encoder == nullptr || frame == nullptr ||
        frame->struct_size < sizeof(mkvc_frame_view) || frame->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or frame view");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->write(*frame, true, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown frame write failure");
    }
}

mkvc_result mkvc_encoder_try_write_frame(mkvc_encoder* encoder,
                                         const mkvc_frame_view* frame) {
    last_error.clear();
    if (encoder == nullptr || frame == nullptr ||
        frame->struct_size < sizeof(mkvc_frame_view) || frame->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid encoder or frame view");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->write(*frame, false, error);
        if (result == MKVC_OK || result == MKVC_WOULD_BLOCK) {
            return result;
        }
        return fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown nonblocking frame write failure");
    }
}

mkvc_result mkvc_encoder_flush(mkvc_encoder* encoder) {
    last_error.clear();
    if (encoder == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "encoder is null");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->flush(error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown encoder flush failure");
    }
}

mkvc_result mkvc_encoder_close(mkvc_encoder* encoder) {
    last_error.clear();
    if (encoder == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "encoder is null");
    }
    try {
        std::string error;
        const mkvc_result result = encoder->implementation->close(error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown encoder close failure");
    }
}

mkvc_result mkvc_encoder_get_metrics(
    const mkvc_encoder* encoder, mkvc_pipeline_metrics* out_metrics) {
    last_error.clear();
    if (encoder == nullptr || out_metrics == nullptr ||
        out_metrics->struct_size < sizeof(mkvc_pipeline_metrics) ||
        out_metrics->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT,
                    "invalid encoder metrics output");
    }
    try {
        mkvc_pipeline_metrics metrics{};
        metrics.struct_size = sizeof(metrics);
        metrics.struct_version = 1;
        encoder->implementation->get_metrics(metrics);
        *out_metrics = metrics;
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown encoder metrics failure");
    }
}

void mkvc_encoder_destroy(mkvc_encoder* encoder) {
    try {
        delete encoder;
    } catch (...) {
    }
}

const char* mkvc_get_last_error(void) {
    return last_error.c_str();
}

mkvc_result mkvc_decoder_create(const mkvc_decoder_config* config,
                                mkvc_decoder** out_decoder) {
    last_error.clear();
    if (out_decoder != nullptr) {
        *out_decoder = nullptr;
    }
    if (config == nullptr || out_decoder == nullptr ||
        config->struct_size < sizeof(mkvc_decoder_config) ||
        config->struct_version != 1 || config->input_path_utf8 == nullptr ||
        config->input_path_utf8[0] == '\0' ||
        (config->codec != MKVC_CODEC_VP9 && config->codec != MKVC_CODEC_AV1) ||
        (config->backend != MKVC_BACKEND_CPU &&
         config->backend != MKVC_BACKEND_INTEL)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder config");
    }
    try {
        std::string error;
        auto handle = std::make_unique<mkvc_decoder>();
        if (config->backend == MKVC_BACKEND_INTEL) {
            handle->intel_implementation =
                mkvc::IntelWebmDecoder::create(*config, error);
            if (!handle->intel_implementation) {
                return fail(MKVC_ERROR_CODEC, std::move(error));
            }
        } else if (config->codec == MKVC_CODEC_VP9) {
            handle->implementation = mkvc::CpuVp9Decoder::create(*config, error);
            if (!handle->implementation) {
                return fail(MKVC_ERROR_CODEC, std::move(error));
            }
        } else {
            handle->av1_implementation = mkvc::CpuAv1Decoder::create(*config, error);
            if (!handle->av1_implementation) {
                return fail(MKVC_ERROR_CODEC, std::move(error));
            }
        }
        handle->capacity = config->prefetch;
        if (handle->capacity > 0) {
            handle->worker = std::thread(decoder_worker, handle.get());
        }
        *out_decoder = handle.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown decoder creation failure");
    }
}

mkvc_result mkvc_decoder_read(mkvc_decoder* decoder, mkvc_frame** out_frame) {
    last_error.clear();
    if (out_frame != nullptr) {
        *out_frame = nullptr;
    }
    if (decoder == nullptr || out_frame == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder or frame output");
    }
    try {
        if (decoder->capacity > 0) {
            std::unique_ptr<mkvc::DecodedFrame> decoded;
            mkvc_result result = MKVC_OK;
            std::string error;
            {
                std::unique_lock<std::mutex> lock(decoder->mutex);
                const auto wait_started = std::chrono::steady_clock::now();
                decoder->not_empty.wait(lock, [decoder] {
                    return decoder->stop_requested || !decoder->queue.empty() ||
                           decoder->worker_finished;
                });
                decoder->queue_wait_ns += elapsed_ns(wait_started);
                if (decoder->stop_requested) {
                    return fail(MKVC_ERROR_INVALID_STATE, "decoder is closing");
                }
                if (!decoder->queue.empty()) {
                    decoded = std::move(decoder->queue.front());
                    decoder->queue.pop_front();
                    ++decoder->completed_frames;
                    decoder->not_full.notify_one();
                } else {
                    result = decoder->worker_result;
                    error = decoder->worker_error;
                }
            }
            if (!decoded) {
                return result == MKVC_END_OF_STREAM
                    ? result
                    : fail(result, std::move(error));
            }
            auto frame = std::make_unique<mkvc_frame>();
            frame->implementation = std::move(decoded);
            *out_frame = frame.release();
            return MKVC_OK;
        }
        std::string error;
        std::unique_ptr<mkvc::DecodedFrame> decoded;
        const auto backend_started = std::chrono::steady_clock::now();
        const mkvc_result result = decoder_read_backend(decoder, decoded, error);
        const uint64_t backend_elapsed = elapsed_ns(backend_started);
        const uint32_t hardware_pending = decoder->intel_implementation
            ? decoder->intel_implementation->max_pending_observed() : 0;
        {
            std::lock_guard<std::mutex> lock(decoder->mutex);
            decoder->backend_time_ns += backend_elapsed;
            decoder->hardware_pending_peak = std::max(
                decoder->hardware_pending_peak, hardware_pending);
            if (result == MKVC_OK) {
                ++decoder->accepted_frames;
                ++decoder->completed_frames;
            }
        }
        if (result == MKVC_END_OF_STREAM) {
            return result;
        }
        if (result != MKVC_OK) {
            return fail(result, std::move(error));
        }
        auto frame = std::make_unique<mkvc_frame>();
        frame->implementation = std::move(decoded);
        *out_frame = frame.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown decoder read failure");
    }
}

mkvc_result mkvc_decoder_close(mkvc_decoder* decoder) {
    last_error.clear();
    if (decoder == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "decoder is null");
    }
    try {
        stop_decoder_worker(decoder);
        std::string error;
        const mkvc_result result = decoder_close_backend(decoder, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown decoder close failure");
    }
}

mkvc_result mkvc_decoder_get_metrics(
    const mkvc_decoder* decoder, mkvc_pipeline_metrics* out_metrics) {
    last_error.clear();
    if (decoder == nullptr || out_metrics == nullptr ||
        out_metrics->struct_size < sizeof(mkvc_pipeline_metrics) ||
        out_metrics->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT,
                    "invalid decoder metrics output");
    }
    try {
        mkvc_pipeline_metrics metrics{};
        metrics.struct_size = sizeof(metrics);
        metrics.struct_version = 1;
        std::lock_guard<std::mutex> lock(decoder->mutex);
        metrics.accepted_frames = decoder->accepted_frames;
        metrics.completed_frames = decoder->completed_frames;
        metrics.queue_wait_ns = decoder->queue_wait_ns;
        metrics.backend_time_ns = decoder->backend_time_ns;
        metrics.queue_capacity = static_cast<uint32_t>(decoder->capacity);
        metrics.peak_queue_depth = decoder->peak_queue_depth;
        metrics.hardware_pending_peak = decoder->hardware_pending_peak;
        metrics.copy_path = decoder->completed_frames == 0
            ? MKVC_COPY_PATH_UNKNOWN : MKVC_COPY_PATH_CPU;
        *out_metrics = metrics;
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown decoder metrics failure");
    }
}

void mkvc_decoder_destroy(mkvc_decoder* decoder) {
    try {
        if (decoder != nullptr) {
            stop_decoder_worker(decoder);
            std::string ignored;
            decoder_close_backend(decoder, ignored);
        }
        delete decoder;
    } catch (...) {
    }
}

void mkvc_frame_retain(mkvc_frame* frame) {
    if (frame != nullptr) {
        frame->references.fetch_add(1, std::memory_order_relaxed);
    }
}

void mkvc_frame_release(mkvc_frame* frame) {
    if (frame != nullptr &&
        frame->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete frame;
    }
}

mkvc_result mkvc_frame_get_view(const mkvc_frame* frame,
                                mkvc_frame_view* out_view) {
    last_error.clear();
    if (frame == nullptr || out_view == nullptr ||
        out_view->struct_size < sizeof(mkvc_frame_view) ||
        out_view->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid frame or output view");
    }
    const auto& source = *frame->implementation;
    out_view->pixel_format = MKVC_PIXEL_FORMAT_I420;
    out_view->width = source.width;
    out_view->height = source.height;
    for (size_t plane = 0; plane < 3; ++plane) {
        out_view->planes[plane] = source.pixels.data() + source.offsets[plane];
        out_view->strides[plane] = source.strides[plane];
    }
    out_view->planes[3] = nullptr;
    out_view->strides[3] = 0;
    out_view->pts = source.pts_ns;
    return MKVC_OK;
}

mkvc_result mkvc_frame_copy_to(const mkvc_frame* frame,
                               mkvc_mutable_frame_view* destination) {
    last_error.clear();
    if (frame == nullptr || destination == nullptr ||
        destination->struct_size < sizeof(mkvc_mutable_frame_view) ||
        destination->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT,
                    "invalid frame or mutable destination view");
    }
    try {
        std::string error;
        const mkvc_result result = mkvc::copy_frame_to(
            *frame->implementation, *destination, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown frame conversion failure");
    }
}

}  // extern "C"
