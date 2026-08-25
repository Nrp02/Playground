#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#include "thread_pool.hpp"

namespace {

std::uint64_t countPrimesUpTo(std::uint64_t limit) {
    std::uint64_t count = 0;
    for (std::uint64_t n = 2; n <= limit; ++n) {
        bool isPrime = true;
        for (std::uint64_t d = 2; d * d <= n; ++d) {
            if (n % d == 0) {
                isPrime = false;
                break;
            }
        }
        if (isPrime) {
            ++count;
        }
    }
    return count;
}

}

int main() {
    const int taskCount = 8;
    const std::uint64_t workSize = 300000;

    auto serialStart = std::chrono::steady_clock::now();
    std::vector<std::uint64_t> serialResults;
    for (int i = 0; i < taskCount; ++i) {
        serialResults.push_back(countPrimesUpTo(workSize));
    }
    auto serialEnd = std::chrono::steady_clock::now();
    auto serialMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        serialEnd - serialStart)
                        .count();

    auto parallelStart = std::chrono::steady_clock::now();
    tp::ThreadPool pool(std::thread::hardware_concurrency() == 0
                             ? 4
                             : std::thread::hardware_concurrency());
    std::vector<std::future<std::uint64_t>> futures;
    for (int i = 0; i < taskCount; ++i) {
        futures.push_back(pool.submit(countPrimesUpTo, workSize));
    }
    std::vector<std::uint64_t> parallelResults;
    for (auto& fut : futures) {
        parallelResults.push_back(fut.get());
    }
    pool.shutdown();
    auto parallelEnd = std::chrono::steady_clock::now();
    auto parallelMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          parallelEnd - parallelStart)
                          .count();

    std::cout << "serial:   " << serialMs << " ms, results:";
    for (auto r : serialResults) {
        std::cout << ' ' << r;
    }
    std::cout << '\n';

    std::cout << "parallel: " << parallelMs << " ms, results:";
    for (auto r : parallelResults) {
        std::cout << ' ' << r;
    }
    std::cout << '\n';

    if (parallelMs > 0) {
        double speedup = static_cast<double>(serialMs) / static_cast<double>(parallelMs);
        std::cout << "speedup: " << speedup << "x\n";
    }

    tp::ThreadPool drainPool(2);
    for (int i = 0; i < 20; ++i) {
        drainPool.submit([i] { return i * i; });
    }
    std::size_t pendingBeforeShutdown = drainPool.pendingTasks();
    drainPool.shutdown();
    std::cout << "queued " << pendingBeforeShutdown
              << " tasks before shutdown; pool drained them all before joining\n";

    return 0;
}
