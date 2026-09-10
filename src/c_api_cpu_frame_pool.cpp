/**
 * @file c_api_cpu_frame_pool.cpp
 * @brief C ABI lifetime and acquire operations for native CPU frame pools.
 */
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "c_api_internal.hpp"

namespace {
#define last_error mkvc_last_error
using mkvc::capi::fail;
}  // namespace

extern "C" {

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
        if (!implementation) return fail(MKVC_ERROR_INVALID_ARGUMENT, std::move(error));
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

}  // extern "C"
