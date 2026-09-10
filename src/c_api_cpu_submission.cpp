/**
 * @file c_api_cpu_submission.cpp
 * @brief C ABI adapters for asynchronous CPU-buffer encoder submissions.
 */
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "c_api_internal.hpp"

namespace {
#define last_error mkvc_last_error
using mkvc::capi::fail;
}  // namespace

extern "C" {

mkvc_result mkvc_submission_query(const mkvc_submission* submission, uint32_t* out_status) {
    last_error.clear();
    if (submission == nullptr || !submission->implementation || out_status == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid submission query");
    }
    std::string error;
    const mkvc_result result = submission->implementation->query(*out_status, error);
    return result == MKVC_OK ? result : fail(result, std::move(error));
}

mkvc_result mkvc_submission_wait(const mkvc_submission* submission, uint32_t timeout_ms) {
    last_error.clear();
    if (submission == nullptr || !submission->implementation) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid submission wait");
    }
    std::string error;
    const mkvc_result result = submission->implementation->wait(timeout_ms, error);
    return result == MKVC_OK ? result : fail(result, std::move(error));
}

void mkvc_submission_release(mkvc_submission* submission) {
    if (submission == nullptr) return;
    if (submission->implementation) {
        std::string ignored;
        submission->implementation->wait(std::numeric_limits<uint32_t>::max(), ignored);
    }
    delete submission;
}

mkvc_result mkvc_encoder_submit_cpu_buffer(mkvc_encoder* encoder, const mkvc_cpu_buffer* buffer,
                                           int64_t pts, mkvc_submission** out_submission) {
    last_error.clear();
    if (encoder == nullptr || buffer == nullptr || !buffer->implementation ||
        out_submission == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT,
                    "invalid encoder, CPU buffer, or submission output");
    }
    *out_submission = nullptr;
    try {
        mkvc_mutable_frame_view mutable_view{};
        std::string error;
        mkvc_result result = buffer->implementation->get_view(mutable_view, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        mkvc_frame_view view{};
        view.struct_size = sizeof(view);
        view.struct_version = 1;
        view.pixel_format = mutable_view.pixel_format;
        view.width = mutable_view.width;
        view.height = mutable_view.height;
        view.pts = pts;
        for (size_t index = 0; index < 4; ++index) {
            view.planes[index] = mutable_view.planes[index];
            view.strides[index] = mutable_view.strides[index];
        }
        auto handle = std::make_unique<mkvc_submission>();
        std::shared_ptr<mkvc::CpuSubmission> state;
        result = encoder->implementation->submit_borrowed(view, state, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        state->set_owner(buffer->implementation);
        handle->implementation = std::move(state);
        *out_submission = handle.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown native CPU buffer submission failure");
    }
}

}  // extern "C"
