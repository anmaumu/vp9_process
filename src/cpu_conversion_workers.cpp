/**
 * @file cpu_conversion_workers.cpp
 * @brief Fixed-size process-wide worker pool for CPU color conversion stripes.
 */
#include "cpu_conversion_workers.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace mkvc {
namespace {

class CpuConversionWorkerPool final {
   public:
    CpuConversionWorkerPool() {
        constexpr unsigned int kMaximumAuxiliaryWorkers = 3;
        const unsigned int hardware_threads = std::max(1u, std::thread::hardware_concurrency());
        const unsigned int worker_count = std::min(kMaximumAuxiliaryWorkers, hardware_threads - 1);
        workers_.reserve(worker_count);
        try {
            for (unsigned int index = 0; index < worker_count; ++index) {
                workers_.emplace_back([this] { run(); });
            }
        } catch (...) {
            shutdown();
            throw;
        }
    }

    ~CpuConversionWorkerPool() { shutdown(); }

    CpuConversionWorkerPool(const CpuConversionWorkerPool&) = delete;
    CpuConversionWorkerPool& operator=(const CpuConversionWorkerPool&) = delete;

    size_t parallelism() const noexcept { return workers_.size() + 1; }

    std::future<int> submit(std::function<int()> operation) {
        auto task = std::make_shared<std::packaged_task<int()>>(std::move(operation));
        auto completion = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.emplace_back([task] { (*task)(); });
        }
        ready_.notify_one();
        return completion;
    }

   private:
    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    void run() noexcept {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
};

CpuConversionWorkerPool& conversion_workers() {
    static CpuConversionWorkerPool workers;
    return workers;
}

}  // namespace

size_t cpu_conversion_parallelism() { return conversion_workers().parallelism(); }

std::future<int> submit_cpu_conversion(std::function<int()> operation) {
    return conversion_workers().submit(std::move(operation));
}

}  // namespace mkvc
