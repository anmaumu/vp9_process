/**
 * @file c_api_cpu.cpp
 * @brief C ABI adapters for CPU frame pools and asynchronous submissions.
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

mkvc_result mkvc_cpu_frame_pool_create(const mkvc_cpu_frame_pool_config* config,
                                       mkvc_cpu_frame_pool** out_pool) {
    last_error.clear();
    if (config == nullptr || out_pool == nullptr || config->struct_size < sizeof(*config) ||
        config->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid native CPU frame pool configuration");
    }
    *out_pool = nullptr;
    try {
        std::string error;
        auto implementation = mkvc::CpuFramePool::create(*config, error);
        if (!implementation) {
            return fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
        }
        auto handle = std::make_unique<mkvc_cpu_frame_pool>();
        handle->implementation = std::move(implementation);
        *out_pool = handle.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown CPU frame pool failure");
    }
}

void mkvc_cpu_frame_pool_destroy(mkvc_cpu_frame_pool* pool) {
    try {
        delete pool;
    } catch (...) {
    }
}

mkvc_result mkvc_cpu_frame_pool_acquire(mkvc_cpu_frame_pool* pool, uint32_t timeout_ms,
                                        mkvc_cpu_buffer** out_buffer) {
    last_error.clear();
    if (pool == nullptr || !pool->implementation || out_buffer == nullptr) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid native CPU frame pool acquire");
    }
    *out_buffer = nullptr;
    try {
        auto handle = std::make_unique<mkvc_cpu_buffer>();
        std::string error;
        const mkvc_result result =
            pool->implementation->acquire(timeout_ms, handle->implementation, error);
        if (result != MKVC_OK) {
            if (result == MKVC_WOULD_BLOCK) return result;
            return fail(result, std::move(error));
        }
        *out_buffer = handle.release();
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown CPU buffer acquire failure");
    }
}

mkvc_result mkvc_cpu_buffer_get_desc(const mkvc_cpu_buffer* buffer,
                                     mkvc_cpu_buffer_desc* out_desc) {
    last_error.clear();
    if (buffer == nullptr || !buffer->implementation || out_desc == nullptr ||
        out_desc->struct_size < sizeof(*out_desc) || out_desc->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid native CPU buffer descriptor output");
    }
    mkvc_cpu_buffer_desc value{};
    value.struct_size = sizeof(value);
    value.struct_version = 1;
    value.pixel_format = buffer->implementation->pixel_format();
    value.width = buffer->implementation->width();
    value.height = buffer->implementation->height();
    value.plane_count = buffer->implementation->plane_count();
    value.generation = buffer->implementation->generation();
    *out_desc = value;
    return MKVC_OK;
}

mkvc_result mkvc_cpu_buffer_get_view(const mkvc_cpu_buffer* buffer,
                                     mkvc_mutable_frame_view* out_view) {
    last_error.clear();
    if (buffer == nullptr || !buffer->implementation || out_view == nullptr ||
        out_view->struct_size < sizeof(*out_view) || out_view->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid native CPU buffer view output");
    }
    try {
        mkvc_mutable_frame_view value{};
        std::string error;
        const mkvc_result result = buffer->implementation->get_view(value, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
        *out_view = value;
        return MKVC_OK;
    } catch (const std::exception& exception) {
        return fail(MKVC_ERROR_INTERNAL, exception.what());
    } catch (...) {
        return fail(MKVC_ERROR_INTERNAL, "unknown CPU buffer view failure");
    }
}

void mkvc_cpu_buffer_release(mkvc_cpu_buffer* buffer) {
    try {
        delete buffer;
    } catch (...) {
    }
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
