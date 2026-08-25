#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace tp {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t numThreads)
        : stopping_(false), acceptingTasks_(true) {
        if (numThreads == 0) {
            numThreads = 1;
        }
        workers_.reserve(numThreads);
        for (std::size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool() {
        shutdown();
    }

    template <typename Func, typename... Args>
    auto submit(Func&& func, Args&&... args)
        -> std::future<std::invoke_result_t<Func, Args...>> {
        using ReturnType = std::invoke_result_t<Func, Args...>;

        auto boundTask = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        std::future<ReturnType> resultFuture = boundTask->get_future();

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (!acceptingTasks_) {
                throw std::runtime_error("submit called after shutdown");
            }
            taskQueue_.emplace_back([boundTask] { (*boundTask)(); });
        }
        queueCondition_.notify_one();
        return resultFuture;
    }

    void shutdown() {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (stopping_) {
                return;
            }
            acceptingTasks_ = false;
            stopping_ = true;
        }
        queueCondition_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    std::size_t pendingTasks() const {
        std::unique_lock<std::mutex> lock(queueMutex_);
        return taskQueue_.size();
    }

    std::size_t workerCount() const {
        return workers_.size();
    }

private:
    void workerLoop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCondition_.wait(lock, [this] {
                    return stopping_ || !taskQueue_.empty();
                });
                if (taskQueue_.empty()) {
                    if (stopping_) {
                        return;
                    }
                    continue;
                }
                task = std::move(taskQueue_.front());
                taskQueue_.pop_front();
            }
            task();
        }
    }

    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::deque<std::function<void()>> taskQueue_;
    std::vector<std::thread> workers_;
    bool stopping_;
    bool acceptingTasks_;
};

}
