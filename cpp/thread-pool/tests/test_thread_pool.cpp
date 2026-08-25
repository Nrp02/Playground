#include <atomic>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../src/thread_pool.hpp"

namespace {

int g_failures = 0;

void expectTrue(bool condition, const std::string& testName) {
    if (!condition) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

template <typename T>
void expectEq(const T& actual, const T& expected, const std::string& testName) {
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

}

int main() {
    {
        tp::ThreadPool pool(4);
        auto fut = pool.submit([] { return 41 + 1; });
        expectEq<int>(fut.get(), 42, "single task returns correct result");
    }

    {
        tp::ThreadPool pool(4);
        std::vector<std::future<int>> futures;
        const int taskCount = 100;
        for (int i = 0; i < taskCount; ++i) {
            futures.push_back(pool.submit([i] { return i * i; }));
        }
        bool allCorrect = true;
        for (int i = 0; i < taskCount; ++i) {
            if (futures[static_cast<std::size_t>(i)].get() != i * i) {
                allCorrect = false;
            }
        }
        expectTrue(allCorrect, "many tasks each return correct result via futures");
    }

    {
        tp::ThreadPool pool(4);
        std::atomic<int> executionCount{0};
        const int taskCount = 500;
        std::vector<std::future<void>> futures;
        for (int i = 0; i < taskCount; ++i) {
            futures.push_back(pool.submit([&executionCount] {
                executionCount.fetch_add(1, std::memory_order_relaxed);
            }));
        }
        for (auto& fut : futures) {
            fut.get();
        }
        expectEq<int>(executionCount.load(), taskCount,
                      "every submitted task executes exactly once");
    }

    {
        tp::ThreadPool pool(2);
        auto fut = pool.submit([]() -> int {
            throw std::runtime_error("boom");
            return 0;
        });
        bool threw = false;
        try {
            fut.get();
        } catch (const std::runtime_error&) {
            threw = true;
        }
        expectTrue(threw, "exception thrown inside a task propagates through the future");
    }

    {
        tp::ThreadPool pool(2);
        pool.shutdown();
        bool threw = false;
        try {
            pool.submit([] { return 1; });
        } catch (const std::runtime_error&) {
            threw = true;
        }
        expectTrue(threw, "submit after shutdown throws");
    }

    {
        tp::ThreadPool pool(1);
        std::atomic<int> executionCount{0};
        const int taskCount = 50;
        for (int i = 0; i < taskCount; ++i) {
            pool.submit([&executionCount] {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                executionCount.fetch_add(1, std::memory_order_relaxed);
            });
        }
        pool.shutdown();
        expectEq<int>(executionCount.load(), taskCount,
                      "shutdown drains already-queued tasks instead of cancelling them");
        expectEq<std::size_t>(pool.pendingTasks(), static_cast<std::size_t>(0),
                               "queue is empty after a draining shutdown");
    }

    {
        tp::ThreadPool pool(2);
        pool.shutdown();
        pool.shutdown();
        expectTrue(true, "calling shutdown twice does not crash or hang");
    }

    {
        tp::ThreadPool pool(8);
        std::atomic<int> executionCount{0};
        const int producerCount = 8;
        const int tasksPerProducer = 200;
        std::vector<std::thread> producers;
        for (int p = 0; p < producerCount; ++p) {
            producers.emplace_back([&pool, &executionCount] {
                for (int i = 0; i < tasksPerProducer; ++i) {
                    try {
                        pool.submit([&executionCount] {
                            executionCount.fetch_add(1, std::memory_order_relaxed);
                        });
                    } catch (const std::runtime_error&) {
                    }
                }
            });
        }
        for (auto& producer : producers) {
            producer.join();
        }
        pool.shutdown();
        expectEq<int>(executionCount.load(), producerCount * tasksPerProducer,
                      "concurrent submitters all have their tasks executed exactly once");
    }

    {
        tp::ThreadPool pool(6);
        expectEq<std::size_t>(pool.workerCount(), static_cast<std::size_t>(6),
                               "worker count matches requested pool size");
    }

    {
        tp::ThreadPool pool(0);
        expectTrue(pool.workerCount() >= 1, "zero requested threads is clamped to at least one");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
