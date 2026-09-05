#include "mkvcodec/mkvc.h"

#include <cassert>
#include <chrono>
#include <thread>

static_assert(sizeof(mkvc_gpu_resource_pool_config) == 16);
static_assert(sizeof(mkvc_gpu_resource_reservation_desc) == 24);
static_assert(sizeof(mkvc_gpu_resource_pool_stats) == 48);

int main() {
    mkvc_gpu_resource_pool_config config{};
    config.struct_size = sizeof(config);
    config.struct_version = 1;
    config.capacity = 1;
    mkvc_gpu_resource_pool* pool = nullptr;
    assert(mkvc_gpu_resource_pool_create(&config, &pool) == MKVC_OK);

    mkvc_gpu_resource_reservation* first = nullptr;
    assert(mkvc_gpu_resource_pool_acquire(pool, 0, &first) == MKVC_OK);
    mkvc_gpu_resource_reservation_desc first_desc{};
    first_desc.struct_size = sizeof(first_desc);
    first_desc.struct_version = 1;
    assert(mkvc_gpu_resource_reservation_get_desc(first, &first_desc) == MKVC_OK);
    assert(first_desc.slot_index == 0 && first_desc.generation == 1);

    mkvc_gpu_resource_reservation* unavailable = nullptr;
    assert(mkvc_gpu_resource_pool_acquire(pool, 0, &unavailable) == MKVC_WOULD_BLOCK);
    assert(unavailable == nullptr);
    assert(mkvc_gpu_resource_pool_acquire(pool, 5, &unavailable) == MKVC_ERROR_TIMEOUT);

    mkvc_gpu_resource_reservation* waited = nullptr;
    std::thread consumer([&] {
        assert(mkvc_gpu_resource_pool_acquire(pool, 1000, &waited) == MKVC_OK);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mkvc_gpu_resource_reservation_release(first);
    consumer.join();
    assert(waited != nullptr);
    mkvc_gpu_resource_reservation_desc second_desc{};
    second_desc.struct_size = sizeof(second_desc);
    second_desc.struct_version = 1;
    assert(mkvc_gpu_resource_reservation_get_desc(waited, &second_desc) == MKVC_OK);
    assert(second_desc.slot_index == 0 && second_desc.generation == 2);

    mkvc_gpu_resource_pool_stats stats{};
    stats.struct_size = sizeof(stats);
    stats.struct_version = 1;
    assert(mkvc_gpu_resource_pool_get_stats(pool, &stats) == MKVC_OK);
    assert(stats.capacity == 1 && stats.in_use == 1 && stats.peak_in_use == 1);
    assert(stats.acquisitions == 2 && stats.rejected_acquisitions == 2 &&
           stats.wait_ns > 0);

    // Destroying the owner does not invalidate an outstanding reservation.
    mkvc_gpu_resource_pool_destroy(pool);
    assert(mkvc_gpu_resource_reservation_get_desc(waited, &second_desc) == MKVC_OK);
    mkvc_gpu_resource_reservation_release(waited);

    config.capacity = 0;
    pool = reinterpret_cast<mkvc_gpu_resource_pool*>(1);
    assert(mkvc_gpu_resource_pool_create(&config, &pool) == MKVC_ERROR_INVALID_ARGUMENT);
    assert(pool == nullptr);

    config.capacity = 1;
    config.reserved = 1;
    assert(mkvc_gpu_resource_pool_create(&config, &pool) == MKVC_ERROR_INVALID_ARGUMENT);
    assert(pool == nullptr);
    return 0;
}
