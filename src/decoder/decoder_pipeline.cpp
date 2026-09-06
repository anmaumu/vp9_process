#include "decoder/decoder_pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>
#include <utility>

#include "backend_registry.hpp"
#include "c_api_internal.hpp"

namespace mkvc::decoder {
namespace {

uint64_t elapsed_ns(std::chrono::steady_clock::time_point started) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count());
}

mkvc_result read_backend(mkvc_decoder& decoder, std::unique_ptr<DecodedFrame>& frame,
                         std::string& error) {
    if (decoder.intel_implementation) return decoder.intel_implementation->read(frame, error);
    if (decoder.nvidia_implementation) return decoder.nvidia_implementation->read(frame, error);
    return decoder.implementation ? decoder.implementation->read(frame, error)
                                  : decoder.av1_implementation->read(frame, error);
}

mkvc_result close_backend(mkvc_decoder& decoder, std::string& error) {
    if (decoder.intel_implementation) return decoder.intel_implementation->close(error);
    if (decoder.nvidia_implementation) return decoder.nvidia_implementation->close(error);
    return decoder.implementation ? decoder.implementation->close(error)
                                  : decoder.av1_implementation->close(error);
}

uint32_t hardware_pending(const mkvc_decoder& decoder) {
    if (decoder.intel_implementation) return decoder.intel_implementation->max_pending_observed();
    return decoder.nvidia_implementation ? decoder.nvidia_implementation->max_pending_observed()
                                         : 0;
}

void worker(mkvc_decoder* decoder) noexcept {
    try {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(decoder->mutex);
                decoder->not_full.wait(lock, [decoder] {
                    return decoder->stop_requested || decoder->queue.size() < decoder->capacity;
                });
                if (decoder->stop_requested) return;
            }

            std::unique_ptr<DecodedFrame> frame;
            std::string error;
            const auto backend_started = std::chrono::steady_clock::now();
            const mkvc_result result = read_backend(*decoder, frame, error);
            const uint64_t backend_elapsed = elapsed_ns(backend_started);
            const uint32_t pending = hardware_pending(*decoder);
            std::lock_guard<std::mutex> lock(decoder->mutex);
            decoder->backend_time_ns += backend_elapsed;
            decoder->hardware_pending_peak = std::max(decoder->hardware_pending_peak, pending);
            if (decoder->stop_requested) return;
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

}  // namespace

mkvc_result create_backend(mkvc_decoder& decoder, const mkvc_decoder_config& config,
                           std::string& error) {
    if (config.backend == MKVC_BACKEND_INTEL) {
        decoder.intel_implementation = IntelWebmDecoder::create(config, error);
        return decoder.intel_implementation ? MKVC_OK : MKVC_ERROR_CODEC;
    }
    if (config.backend == MKVC_BACKEND_NVIDIA) {
        const auto& capabilities = backend_capabilities();
        const bool available =
            std::any_of(capabilities.begin(), capabilities.end(), [&config](const auto& item) {
                return item.backend == MKVC_BACKEND_NVIDIA && item.codec == config.codec &&
                       item.can_decode != 0;
            });
        if (!available) {
            error = "requested NVIDIA decode capability is unavailable";
            return MKVC_ERROR_NOT_SUPPORTED;
        }
        decoder.nvidia_implementation = NvidiaWebmDecoder::create(config, error);
        return decoder.nvidia_implementation ? MKVC_OK : MKVC_ERROR_CODEC;
    }
    if (config.codec == MKVC_CODEC_VP9) {
        decoder.implementation = CpuVp9Decoder::create(config, error);
        return decoder.implementation ? MKVC_OK : MKVC_ERROR_CODEC;
    }
    decoder.av1_implementation = CpuAv1Decoder::create(config, error);
    return decoder.av1_implementation ? MKVC_OK : MKVC_ERROR_CODEC;
}

void start_prefetch(mkvc_decoder& decoder) {
    if (decoder.capacity > 0) decoder.worker = std::thread(worker, &decoder);
}

void stop_prefetch(mkvc_decoder& decoder) {
    if (decoder.capacity == 0) return;
    {
        std::lock_guard<std::mutex> lock(decoder.mutex);
        decoder.stop_requested = true;
        decoder.not_empty.notify_all();
        decoder.not_full.notify_all();
    }
    if (decoder.worker.joinable()) decoder.worker.join();
    std::lock_guard<std::mutex> lock(decoder.mutex);
    decoder.queue.clear();
}

mkvc_result read_cpu(mkvc_decoder& decoder, std::unique_ptr<DecodedFrame>& frame,
                     std::string& error) {
    frame.reset();
    if (decoder.capacity > 0) {
        std::unique_lock<std::mutex> lock(decoder.mutex);
        const auto wait_started = std::chrono::steady_clock::now();
        decoder.not_empty.wait(lock, [&decoder] {
            return decoder.stop_requested || !decoder.queue.empty() || decoder.worker_finished;
        });
        decoder.queue_wait_ns += elapsed_ns(wait_started);
        if (decoder.stop_requested) {
            error = "decoder is closing";
            return MKVC_ERROR_INVALID_STATE;
        }
        if (!decoder.queue.empty()) {
            frame = std::move(decoder.queue.front());
            decoder.queue.pop_front();
            ++decoder.completed_frames;
            decoder.not_full.notify_one();
            return MKVC_OK;
        }
        error = decoder.worker_error;
        return decoder.worker_result;
    }

    const auto backend_started = std::chrono::steady_clock::now();
    const mkvc_result result = read_backend(decoder, frame, error);
    const uint64_t backend_elapsed = elapsed_ns(backend_started);
    const uint32_t pending = hardware_pending(decoder);
    {
        std::lock_guard<std::mutex> lock(decoder.mutex);
        decoder.backend_time_ns += backend_elapsed;
        decoder.hardware_pending_peak = std::max(decoder.hardware_pending_peak, pending);
        if (result == MKVC_OK) {
            ++decoder.accepted_frames;
            ++decoder.completed_frames;
        }
    }
    return result;
}

mkvc_result read_gpu(mkvc_decoder& decoder, mkvc_gpu_frame** frame, std::string& error) {
    const auto started = std::chrono::steady_clock::now();
    const mkvc_result result = decoder.intel_implementation
                                   ? decoder.intel_implementation->read_gpu(frame, error)
                                   : decoder.nvidia_implementation->read_gpu(frame, error);
    {
        std::lock_guard<std::mutex> lock(decoder.mutex);
        decoder.backend_time_ns += elapsed_ns(started);
        if (result == MKVC_OK) {
            ++decoder.accepted_frames;
            ++decoder.completed_frames;
            decoder.gpu_path_exercised = true;
        }
    }
    return result;
}

mkvc_result close(mkvc_decoder& decoder, std::string& error) {
    stop_prefetch(decoder);
    return close_backend(decoder, error);
}

}  // namespace mkvc::decoder
