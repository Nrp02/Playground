#include "thread_pool.hpp"

#include <chrono>

namespace cq {

ThreadPool::ThreadPool(size_t numThreads, size_t queueCapacity) : queue_(queueCapacity) {
    workers_.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    stop_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

void ThreadPool::workerLoop() {
    std::function<void()> job;
    while (true) {
        if (queue_.tryDequeue(job)) {
            job();
            size_t remaining = pending_.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (remaining == 0) {
                std::lock_guard<std::mutex> lock(idleMutex_);
                idleCv_.notify_all();
            }
            continue;
        }
        if (stop_.load(std::memory_order_relaxed)) {
            if (!queue_.tryDequeue(job)) break;
            job();
            pending_.fetch_sub(1, std::memory_order_acq_rel);
            continue;
        }
        std::unique_lock<std::mutex> lock(cvMutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(1));
    }
}

void ThreadPool::waitIdle() {
    std::unique_lock<std::mutex> lock(idleMutex_);
    idleCv_.wait(lock, [this]() { return pending_.load(std::memory_order_acquire) == 0; });
}

}
