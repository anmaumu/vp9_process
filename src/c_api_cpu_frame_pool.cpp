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

mkvc_result create_pool(const mkvc_cpu_frame_pool_config& config, uint32_t memory_mode,
                        mkvc_cpu_frame_pool** out_pool) {
    try {
        std::string error;
        std::shared_ptr<mkvc::CpuFramePool> implementation;
        const mkvc_result result =
            mkvc::CpuFramePool::create(config, memory_mode, implementation, error);
        if (result != MKVC_OK) return fail(result, std::move(error));
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
    return create_pool(*config, MKVC_CPU_MEMORY_PAGEABLE, out_pool);
}

mkvc_result mkvc_cpu_frame_pool_create_ex(const mkvc_cpu_frame_pool_config* config,
                                          const mkvc_cpu_frame_pool_options* options,
                                          mkvc_cpu_frame_pool** out_pool) {
    last_error.clear();
    if (out_pool != nullptr) *out_pool = nullptr;
    if (config == nullptr || options == nullptr || out_pool == nullptr ||
        config->struct_size < sizeof(*config) || config->struct_version != 1 ||
        options->struct_size < sizeof(*options) || options->struct_version != 1 ||
        options->reserved != 0) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid native CPU frame pool options");
    }
    return create_pool(*config, options->memory_mode, out_pool);
}

mkvc_result mkvc_cpu_frame_pool_get_stats(const mkvc_cpu_frame_pool* pool,
                                          mkvc_cpu_frame_pool_stats* out_stats) {
    last_error.clear();
    if (pool == nullptr || !pool->implementation || out_stats == nullptr ||
        out_stats->struct_size < sizeof(*out_stats) || out_stats->struct_version != 1) {
        return fail(MKVC_ERROR_INVALID_ARGUMENT, "invalid native CPU frame pool statistics output");
    }
    pool->implementation->snapshot_stats(*out_stats);
    return MKVC_OK;
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
