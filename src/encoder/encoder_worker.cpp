#include "encoder_worker.hpp"

#include <algorithm>
#include <exception>
#include <thread>
#include <utility>

namespace mkvc::encoder {
namespace {

void fail_queued_submissions(EncoderSession::Impl& impl, mkvc_result result,
                             const std::string& error) noexcept {
    for (auto& queued : impl.queue) {
        if (queued.submission) queued.submission->complete(result, error);
    }
}

}  // namespace

mkvc_result backend_write(EncoderSession::Impl& impl, const mkvc_frame_view& frame,
                          std::string& error) {
    return impl.backend->write(frame, error);
}

mkvc_result backend_flush(EncoderSession::Impl& impl, std::string& error) {
    return impl.backend->flush(error);
}

mkvc_result backend_close(EncoderSession::Impl& impl, std::string& error) {
    return impl.backend->close(error);
}

uint32_t backend_hardware_pending(const EncoderSession::Impl& impl) {
    return impl.backend->hardware_pending();
}

uint64_t elapsed_ns(std::chrono::steady_clock::time_point started) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count());
}

void observe_copy_path(EncoderSession::Impl& impl, uint32_t path) {
    if (impl.copy_path == MKVC_COPY_PATH_UNKNOWN)
        impl.copy_path = path;
    else if (impl.copy_path != path)
        impl.copy_path = MKVC_COPY_PATH_MIXED;
}

void run_encoder_worker(EncoderSession::Impl* impl) noexcept {
    std::shared_ptr<CpuSubmission> active_submission;
    try {
        while (true) {
            EncoderSession::Impl::Item item;
            bool inject_failure = false;
            {
                std::unique_lock<std::mutex> lock(impl->mutex);
                impl->has_items.wait(
                    lock, [impl] { return impl->close_requested || !impl->queue.empty(); });
                if (impl->queue.empty() && impl->close_requested) break;
                item = std::move(impl->queue.front());
                impl->queue.pop_front();
                active_submission = item.submission;
#if defined(MKVC_ENABLE_TEST_HOOKS)
                inject_failure = item.type == EncoderSession::Impl::ItemType::kFrame &&
                                 impl->completed_frames >= impl->test_fail_after;
#endif
                impl->has_space.notify_all();
            }

            std::string error;
            mkvc_result result = MKVC_OK;
            const auto backend_started = std::chrono::steady_clock::now();
            if (item.type == EncoderSession::Impl::ItemType::kFrame) {
#if defined(MKVC_ENABLE_TEST_HOOKS)
                if (impl->test_delay_ms != 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(impl->test_delay_ms));
                }
#endif
                if (inject_failure) {
                    result = MKVC_ERROR_IO;
                    error = "injected asynchronous backend device loss";
                } else {
                    const mkvc_frame_view view =
                        item.submission ? item.borrowed : item.frame->view();
                    result = backend_write(*impl, view, error);
                }
            } else {
                result = backend_flush(*impl, error);
            }
            const uint64_t backend_elapsed = elapsed_ns(backend_started);
            const uint32_t hardware_pending = backend_hardware_pending(*impl);
            {
                std::lock_guard<std::mutex> lock(impl->mutex);
                impl->backend_time_ns += backend_elapsed;
                impl->hardware_pending_peak =
                    std::max(impl->hardware_pending_peak, hardware_pending);
                if (item.frame) {
                    impl->free_frames.push_back(std::move(item.frame));
                    impl->has_space.notify_all();
                }
                if (result != MKVC_OK) {
                    if (item.submission) item.submission->complete(result, error);
                    fail_queued_submissions(*impl, result, error);
                    impl->failed = true;
                    impl->accepting = false;
                    impl->terminal_result = result;
                    impl->terminal_error = std::move(error);
                    impl->queue.clear();
                    impl->state_changed.notify_all();
                    impl->has_space.notify_all();
                    break;
                }
                if (item.type == EncoderSession::Impl::ItemType::kFlush) {
                    impl->completed_flush_token = item.flush_token;
                    impl->state_changed.notify_all();
                } else {
                    if (item.submission) item.submission->complete(MKVC_OK, {});
                    ++impl->completed_frames;
                    observe_copy_path(*impl, MKVC_COPY_PATH_CPU);
                }
                active_submission.reset();
            }
        }

        std::string error;
        const auto close_started = std::chrono::steady_clock::now();
        const mkvc_result close_result = backend_close(*impl, error);
        const uint64_t close_elapsed = elapsed_ns(close_started);
        const uint32_t hardware_pending = backend_hardware_pending(*impl);
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->backend_time_ns += close_elapsed;
        impl->hardware_pending_peak = std::max(impl->hardware_pending_peak, hardware_pending);
        if (!impl->failed && close_result != MKVC_OK) {
            impl->failed = true;
            impl->terminal_result = close_result;
            impl->terminal_error = std::move(error);
        }
        impl->closed = true;
        impl->accepting = false;
        impl->state_changed.notify_all();
        impl->has_space.notify_all();
    } catch (const std::exception& exception) {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (active_submission) {
            active_submission->complete(MKVC_ERROR_INTERNAL, exception.what());
        }
        fail_queued_submissions(*impl, MKVC_ERROR_INTERNAL, exception.what());
        impl->failed = true;
        impl->closed = true;
        impl->accepting = false;
        impl->terminal_result = MKVC_ERROR_INTERNAL;
        impl->terminal_error = exception.what();
        impl->state_changed.notify_all();
        impl->has_space.notify_all();
    } catch (...) {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (active_submission) {
            active_submission->complete(MKVC_ERROR_INTERNAL,
                                        "unknown asynchronous encoder failure");
        }
        fail_queued_submissions(*impl, MKVC_ERROR_INTERNAL, "unknown asynchronous encoder failure");
        impl->failed = true;
        impl->closed = true;
        impl->accepting = false;
        impl->terminal_result = MKVC_ERROR_INTERNAL;
        impl->terminal_error = "unknown asynchronous encoder failure";
        impl->state_changed.notify_all();
        impl->has_space.notify_all();
    }
}

}  // namespace mkvc::encoder
