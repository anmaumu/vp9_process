#include "encoder/encoder_queue_control.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include "encoder/encoder_backend_execution.hpp"

namespace mkvc::encoder {
namespace {

mkvc_result stopped_result(const EncoderSession::Impl& impl, std::string& error) {
    error = impl.canceled ? "encoder was cancelled" : "encoder is closing or closed";
    return impl.canceled ? MKVC_ERROR_CANCELLED : MKVC_ERROR_INVALID_STATE;
}

}  // namespace

mkvc_result enqueue_owned(EncoderSession::Impl& impl, const mkvc_frame_view& frame, bool block,
                          std::string& error) {
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        if (impl.failed) {
            error = impl.terminal_error;
            return impl.terminal_result;
        }
        if (!impl.accepting) return stopped_result(impl, error);
    }
    if (frame.width != impl.width || frame.height != impl.height) {
        error = "frame dimensions do not match encoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }

    std::unique_ptr<OwnedFrame> owned;
    {
        std::unique_lock<std::mutex> lock(impl.mutex);
        if (!block && (impl.queue.size() >= impl.capacity || impl.free_frames.empty())) {
            ++impl.rejected_frames;
            return MKVC_WOULD_BLOCK;
        }
        if (block) {
            const auto wait_started = std::chrono::steady_clock::now();
            impl.has_space.wait(lock, [&impl] {
                return (impl.queue.size() < impl.capacity && !impl.free_frames.empty()) ||
                       !impl.accepting || impl.failed;
            });
            impl.queue_wait_ns += elapsed_ns(wait_started);
        }
        if (impl.failed) {
            error = impl.terminal_error;
            return impl.terminal_result;
        }
        if (!impl.accepting) return stopped_result(impl, error);
        if (impl.queue.size() >= impl.capacity || impl.free_frames.empty()) {
            ++impl.rejected_frames;
            return MKVC_WOULD_BLOCK;
        }
        owned = std::move(impl.free_frames.front());
        impl.free_frames.pop_front();
    }

    mkvc_result result = own_frame(frame, *owned, error);
    if (result != MKVC_OK) {
        std::lock_guard<std::mutex> lock(impl.mutex);
        impl.free_frames.push_back(std::move(owned));
        impl.has_space.notify_all();
        return result;
    }

    std::unique_lock<std::mutex> lock(impl.mutex);
    if (block) {
        const auto wait_started = std::chrono::steady_clock::now();
        impl.has_space.wait(lock, [&impl] {
            return impl.queue.size() < impl.capacity || !impl.accepting || impl.failed;
        });
        impl.queue_wait_ns += elapsed_ns(wait_started);
    }
    if (impl.failed) {
        impl.free_frames.push_back(std::move(owned));
        error = impl.terminal_error;
        return impl.terminal_result;
    }
    if (!impl.accepting) {
        impl.free_frames.push_back(std::move(owned));
        return stopped_result(impl, error);
    }
    if (impl.queue.size() >= impl.capacity) {
        impl.free_frames.push_back(std::move(owned));
        impl.has_space.notify_all();
        ++impl.rejected_frames;
        return MKVC_WOULD_BLOCK;
    }
    EncoderSession::Impl::Item item;
    item.frame = std::move(owned);
    impl.queue.push_back(std::move(item));
    ++impl.accepted_frames;
    impl.peak_queue_depth =
        std::max<uint32_t>(impl.peak_queue_depth, static_cast<uint32_t>(impl.queue.size()));
    impl.has_items.notify_one();
    return MKVC_OK;
}

mkvc_result enqueue_borrowed(EncoderSession::Impl& impl, const mkvc_frame_view& frame,
                             std::shared_ptr<CpuSubmission>& submission, std::string& error) {
    if (frame.width != impl.width || frame.height != impl.height) {
        error = "frame dimensions do not match encoder configuration";
        return MKVC_ERROR_INVALID_ARGUMENT;
    }
    const mkvc_result validation = validate_borrowed_frame(frame, error);
    if (validation != MKVC_OK) return validation;
    auto state = std::make_shared<CpuSubmission>();
    std::unique_lock<std::mutex> lock(impl.mutex);
    if (impl.capacity == 0) {
        error = "async borrowed submission requires queue_size greater than zero";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    if (impl.require_gpu_resident || !impl.allow_cpu_copy) {
        error = "CPU frame submission is prohibited by copy policy";
        return MKVC_ERROR_NOT_SUPPORTED;
    }
    const auto wait_started = std::chrono::steady_clock::now();
    impl.has_space.wait(lock, [&impl] {
        return impl.queue.size() < impl.capacity || !impl.accepting || impl.failed;
    });
    impl.queue_wait_ns += elapsed_ns(wait_started);
    if (impl.failed) {
        error = impl.terminal_error;
        return impl.terminal_result;
    }
    if (!impl.accepting) return stopped_result(impl, error);
    EncoderSession::Impl::Item item;
    item.borrowed = frame;
    item.submission = state;
    impl.queue.push_back(std::move(item));
    ++impl.accepted_frames;
    impl.peak_queue_depth =
        std::max<uint32_t>(impl.peak_queue_depth, static_cast<uint32_t>(impl.queue.size()));
    submission = std::move(state);
    impl.has_items.notify_one();
    return MKVC_OK;
}

mkvc_result flush_and_wait(EncoderSession::Impl& impl, std::string& error) {
    std::unique_lock<std::mutex> lock(impl.mutex);
    impl.has_space.wait(lock, [&impl] {
        return impl.queue.size() < impl.capacity || !impl.accepting || impl.failed;
    });
    if (impl.failed) {
        error = impl.terminal_error;
        return impl.terminal_result;
    }
    if (!impl.accepting) return stopped_result(impl, error);
    const uint64_t token = ++impl.next_flush_token;
    EncoderSession::Impl::Item item;
    item.type = EncoderSession::Impl::ItemType::kFlush;
    item.flush_token = token;
    impl.queue.push_back(std::move(item));
    impl.has_items.notify_one();
    impl.state_changed.wait(lock, [&impl, token] {
        return impl.completed_flush_token >= token || impl.failed || impl.canceled;
    });
    if (impl.failed) {
        error = impl.terminal_error;
        return impl.terminal_result;
    }
    if (impl.canceled) {
        error = "encoder was cancelled";
        return MKVC_ERROR_CANCELLED;
    }
    return MKVC_OK;
}

mkvc_result cancel(EncoderSession::Impl& impl, std::string& error) {
    std::lock_guard<std::mutex> lock(impl.mutex);
    if (impl.closed || impl.canceled) return MKVC_OK;
    if (impl.failed) {
        error = impl.terminal_error;
        return impl.terminal_result;
    }
    impl.canceled = true;
    impl.accepting = false;
    if (impl.capacity != 0) impl.close_requested = true;
    for (auto& item : impl.queue) {
        if (item.submission) {
            item.submission->complete(MKVC_ERROR_CANCELLED, "encoder submission was cancelled");
        }
        if (item.frame) impl.free_frames.push_back(std::move(item.frame));
    }
    impl.queue.clear();
    impl.has_items.notify_all();
    impl.has_space.notify_all();
    impl.state_changed.notify_all();
    return MKVC_OK;
}

mkvc_result close_and_join(EncoderSession::Impl& impl, std::string& error) {
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        if (!impl.close_requested) {
            impl.accepting = false;
            impl.close_requested = true;
            impl.has_items.notify_all();
            impl.has_space.notify_all();
        }
    }
    if (impl.worker.joinable()) impl.worker.join();
    std::lock_guard<std::mutex> lock(impl.mutex);
    if (impl.failed) {
        error = impl.terminal_error;
        return impl.terminal_result;
    }
    return MKVC_OK;
}

}  // namespace mkvc::encoder
