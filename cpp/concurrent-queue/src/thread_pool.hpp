#ifndef CONCURRENT_QUEUE_THREAD_POOL_HPP
#define CONCURRENT_QUEUE_THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "lockfree_queue.hpp"

namespace cq {

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads, size_t queueCapacity = 1 << 16);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<ReturnType> result = task->get_future();

        std::function<void()> job = [task]() { (*task)(); };
        while (!queue_.tryEnqueue(std::move(job))) {
            std::this_thread::yield();
        }
        pending_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(cvMutex_);
        }
        cv_.notify_one();
        return result;
    }

    void waitIdle();
    size_t pendingCount() const { return pending_.load(std::memory_order_relaxed); }

private:
    void workerLoop();

    MpmcBoundedQueue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> pending_{0};

    std::mutex cvMutex_;
    std::condition_variable cv_;

    std::mutex idleMutex_;
    std::condition_variable idleCv_;
};

}

#endif
