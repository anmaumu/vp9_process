#include "decoder/decoder_prefetch.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>
#include <utility>

#include "c_api_internal.hpp"
#include "decoder/decoder_backend_io.hpp"

namespace mkvc::decoder {
namespace {

uint64_t elapsed_ns(std::chrono::steady_clock::time_point started) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count());
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

mkvc_result read_prefetched(mkvc_decoder& decoder, std::unique_ptr<DecodedFrame>& frame,
                            std::string& error) {
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

}  // namespace mkvc::decoder
