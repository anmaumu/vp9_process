/**
 * @file c_api_decoder.cpp
 * @brief C ABI adapters for decode, prefetch, frame access, and processing.
 */
#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "backend_registry.hpp"
#include "c_api_internal.hpp"
#include "frame_conversion.hpp"
#include "frame_processor.hpp"
#include "gpu/gpu_frame.hpp"

namespace {
#define last_error mkvc_last_error
using mkvc::capi::fail;

uint64_t elapsed_ns(std::chrono::steady_clock::time_point started) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count());
}

mkvc_result decoder_read_backend(mkvc_decoder* decoder, std::unique_ptr<mkvc::DecodedFrame>& frame,
                                 std::string& error) {
    if (decoder->intel_implementation) return decoder->intel_implementation->read(frame, error);
    if (decoder->nvidia_implementation) return decoder->nvidia_implementation->read(frame, error);
    return decoder->implementation ? decoder->implementation->read(frame, error)
                                   : decoder->av1_implementation->read(frame, error);
}

mkvc_result decoder_close_backend(mkvc_decoder* decoder, std::string& error) {
    if (decoder->intel_implementation) return decoder->intel_implementation->close(error);
    if (decoder->nvidia_implementation) return decoder->nvidia_implementation->close(error);
    return decoder->implementation ? decoder->implementation->close(error)
                                   : decoder->av1_implementation->close(error);
}

void decoder_worker(mkvc_decoder* decoder) noexcept {
    try {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(decoder->mutex);
                decoder->not_full.wait(lock, [decoder] {
                    return decoder->stop_requested || decoder->queue.size() < decoder->capacity;
                });
                if (decoder->stop_requested) {
                    return;
                }
            }

            std::unique_ptr<mkvc::DecodedFrame> frame;
            std::string error;
            const auto backend_started = std::chrono::steady_clock::now();
            const mkvc_result result = decoder_read_backend(decoder, frame, error);
            const uint64_t backend_elapsed = elapsed_ns(backend_started);
            const uint32_t hardware_pending =
                decoder->intel_implementation
                    ? decoder->intel_implementation->max_pending_observed()
                    : (decoder->nvidia_implementation
                           ? decoder->nvidia_implementation->max_pending_observed()
                           : 0);
            std::lock_guard<std::mutex> lock(decoder->mutex);
            decoder->backend_time_ns += backend_elapsed;
            decoder->hardware_pending_peak =
                std::max(decoder->hardware_pending_peak, hardware_pending);
            if (decoder->stop_requested) {
                return;
            }
            if (result == MKVC_OK) {
                decoder->queue.push_back(std::move(frame));
                ++decoder->accepted_frames;
                decoder->peak_queue_depth = std::max<uint32_t>(
                    decoder->peak_queue_depth, static_cast<uint32_t>(decoder->queue.size()));
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

mkvc_result mkvc_decoder_create(const mkvc_decoder_config* config, mkvc_decoder** out_decoder) {
    last_error.clear();
    if (out_decoder != nullptr) {
        *out_decoder = nullptr;
    }
    if (config == nullptr || out_decoder == nullptr ||
        config->struct_size < sizeof(mkvc_decoder_config) || config->struct_version != 1 ||
        config->input_path_utf8 == nullptr || config->input_path_utf8[0] == '\0' ||
        (config->codec != MKVC_CODEC_VP9 && config->codec != MKVC_CODEC_AV1) ||
        (config->backend != MKVC_BACKEND_CPU && config->backend != MKVC_BACKEND_INTEL &&
         config->backend != MKVC_BACKEND_NVIDIA)) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder config");
    }
    try {
        std::string error;
        auto handle = std::make_unique<mkvc_decoder>();
        if (config->backend == MKVC_BACKEND_INTEL) {
            handle->intel_implementation = mkvc::IntelWebmDecoder::create(*config, error);
            if (!handle->intel_implementation) {
                return fail(MKVC_ERROR_CODEC, std::move(error));
            }
        } else if (config->backend == MKVC_BACKEND_NVIDIA) {
            const auto& capabilities = mkvc::backend_capabilities();
            const bool available =
                std::any_of(capabilities.begin(), capabilities.end(), [config](const auto& item) {
                    return item.backend == MKVC_BACKEND_NVIDIA && item.codec == config->codec &&
                           item.can_decode != 0;
                });
            if (!available) {
                return fail(MKVC_ERROR_NOT_SUPPORTED,
                            "requested NVIDIA decode capability is unavailable");
            }
            handle->nvidia_implementation = mkvc::NvidiaWebmDecoder::create(*config, error);
            if (!handle->nvidia_implementation) {
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

mkvc_result mkvc_decoder_set_copy_policy(mkvc_decoder* decoder, const mkvc_copy_policy* policy) {
    last_error.clear();
    if (decoder == nullptr || policy == nullptr || policy->struct_size < sizeof(*policy) ||
        policy->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder copy policy");
    }
    std::lock_guard<std::mutex> lock(decoder->mutex);
    if (decoder->accepted_frames != 0 || decoder->completed_frames != 0) {
        return fail(MKVC_ERROR_INVALID_STATE,
                    "copy policy must be set before the first decoder frame");
    }
    if ((policy->require_gpu_resident != 0 || policy->allow_cpu_copy == 0) &&
        decoder->capacity != 0) {
        return fail(MKVC_ERROR_NOT_SUPPORTED,
                    "GPU-resident decoding currently requires prefetch=0");
    }
    if (policy->require_gpu_resident != 0 && policy->allow_cpu_copy != 0) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT,
                    "require_gpu_resident conflicts with allow_cpu_copy");
    }
    if (policy->require_gpu_resident != 0 && !decoder->intel_implementation &&
        !decoder->nvidia_implementation) {
        return fail(MKVC_ERROR_NOT_SUPPORTED,
                    "GPU-resident decoding is unavailable for this backend");
    }
    decoder->require_gpu_resident = policy->require_gpu_resident != 0;
    decoder->allow_gpu_copy = policy->allow_gpu_copy != 0;
    decoder->allow_cpu_copy = policy->allow_cpu_copy != 0;
    return MKVC_OK;
}

mkvc_result mkvc_decoder_read(mkvc_decoder* decoder, mkvc_frame** out_frame) {
    last_error.clear();
    if (out_frame != nullptr) {
        *out_frame = nullptr;
    }
    if (decoder == nullptr || out_frame == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder or frame output");
    }
    {
        std::lock_guard<std::mutex> lock(decoder->mutex);
        if (decoder->require_gpu_resident || !decoder->allow_cpu_copy) {
            return fail(MKVC_ERROR_NOT_SUPPORTED, "CPU frame read is prohibited by copy policy");
        }
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
                return result == MKVC_END_OF_STREAM ? result : fail(result, std::move(error));
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
        const uint32_t hardware_pending =
            decoder->intel_implementation
                ? decoder->intel_implementation->max_pending_observed()
                : (decoder->nvidia_implementation
                       ? decoder->nvidia_implementation->max_pending_observed()
                       : 0);
        {
            std::lock_guard<std::mutex> lock(decoder->mutex);
            decoder->backend_time_ns += backend_elapsed;
            decoder->hardware_pending_peak =
                std::max(decoder->hardware_pending_peak, hardware_pending);
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

mkvc_result mkvc_decoder_read_gpu(mkvc_decoder* decoder, mkvc_gpu_frame** out_frame) {
    last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (decoder == nullptr || out_frame == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder or GPU frame output");
    }
    if (decoder->capacity != 0) {
        return fail(MKVC_ERROR_NOT_SUPPORTED, "GPU read currently requires decoder prefetch=0");
    }
    if (!decoder->intel_implementation && !decoder->nvidia_implementation) {
        return fail(MKVC_ERROR_NOT_SUPPORTED,
                    "GPU read is not implemented for this decoder backend");
    }
    try {
        std::string error;
        const auto started = std::chrono::steady_clock::now();
        const mkvc_result result = decoder->intel_implementation
                                       ? decoder->intel_implementation->read_gpu(out_frame, error)
                                       : decoder->nvidia_implementation->read_gpu(out_frame, error);
        {
            std::lock_guard<std::mutex> lock(decoder->mutex);
            decoder->backend_time_ns += elapsed_ns(started);
            if (result == MKVC_OK) {
                ++decoder->accepted_frames;
                ++decoder->completed_frames;
                decoder->gpu_path_exercised = true;
            }
        }
        return result == MKVC_OK || result == MKVC_END_OF_STREAM ? result
                                                                 : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown GPU decoder read failure");
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

mkvc_result mkvc_decoder_get_metrics(const mkvc_decoder* decoder,
                                     mkvc_pipeline_metrics* out_metrics) {
    last_error.clear();
    if (decoder == nullptr || out_metrics == nullptr ||
        out_metrics->struct_size < sizeof(mkvc_pipeline_metrics) ||
        out_metrics->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid decoder metrics output");
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
        metrics.copy_path =
            decoder->completed_frames == 0 ? MKVC_COPY_PATH_UNKNOWN : MKVC_COPY_PATH_CPU;
        if (decoder->gpu_path_exercised) {
            metrics.copy_path = MKVC_COPY_PATH_ZERO_COPY;
        }
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
    if (frame != nullptr && frame->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete frame;
    }
}

mkvc_result mkvc_frame_get_view(const mkvc_frame* frame, mkvc_frame_view* out_view) {
    last_error.clear();
    if (frame == nullptr || out_view == nullptr ||
        out_view->struct_size < sizeof(mkvc_frame_view) || out_view->struct_version != 1) {
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

mkvc_result mkvc_frame_copy_to(const mkvc_frame* frame, mkvc_mutable_frame_view* destination) {
    last_error.clear();
    if (frame == nullptr || destination == nullptr ||
        destination->struct_size < sizeof(mkvc_mutable_frame_view) ||
        destination->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid frame or mutable destination view");
    }
    try {
        std::string error;
        const mkvc_result result = mkvc::copy_frame_to(*frame->implementation, *destination, error);
        return result == MKVC_OK ? result : fail(result, std::move(error));
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown frame conversion failure");
    }
}

mkvc_result mkvc_frame_process(const mkvc_frame* frame, const mkvc_frame_process_config* config,
                               mkvc_frame** out_frame) {
    last_error.clear();
    if (out_frame != nullptr) *out_frame = nullptr;
    if (frame == nullptr || frame->implementation == nullptr || config == nullptr ||
        out_frame == nullptr || config->struct_size < sizeof(mkvc_frame_process_config) ||
        config->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid frame process arguments");
    }
    try {
        std::unique_ptr<mkvc::DecodedFrame> processed;
        std::string error;
        const mkvc_result result =
            mkvc::process_frame_cpu(*frame->implementation, *config, processed, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        auto handle = std::make_unique<mkvc_frame>();
        handle->implementation = std::move(processed);
        *out_frame = handle.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown frame processing failure");
    }
}

}  // extern "C"
