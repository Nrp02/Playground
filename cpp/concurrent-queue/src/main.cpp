#include <atomic>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#include "thread_pool.hpp"

namespace {

bool runCounterStressTest(size_t numProducers, size_t tasksPerProducer, size_t numWorkers) {
    cq::ThreadPool pool(numWorkers);
    std::atomic<long long> counter{0};

    std::vector<std::thread> producers;
    producers.reserve(numProducers);
    for (size_t p = 0; p < numProducers; ++p) {
        producers.emplace_back([&pool, &counter, tasksPerProducer]() {
            for (size_t i = 0; i < tasksPerProducer; ++i) {
                pool.submit([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
            }
        });
    }
    for (auto& t : producers) t.join();

    pool.waitIdle();

    long long expected = static_cast<long long>(numProducers * tasksPerProducer);
    long long actual = counter.load(std::memory_order_seq_cst);
    std::cout << "  counter test: expected=" << expected << " actual=" << actual
              << (expected == actual ? "  OK" : "  MISMATCH") << '\n';
    return expected == actual;
}

bool runFutureSumTest(size_t numTasks, size_t numWorkers) {
    cq::ThreadPool pool(numWorkers);
    std::vector<std::future<long long>> futures;
    futures.reserve(numTasks);

    for (size_t i = 1; i <= numTasks; ++i) {
        futures.push_back(pool.submit(
            [](long long n) {
                long long sum = 0;
                for (long long k = 1; k <= n; ++k) sum += k;
                return sum;
            },
            static_cast<long long>(i % 500 + 1)));
    }

    long long total = 0;
    for (auto& f : futures) total += f.get();

    long long expected = 0;
    for (size_t i = 1; i <= numTasks; ++i) {
        long long n = static_cast<long long>(i % 500 + 1);
        expected += n * (n + 1) / 2;
    }

    std::cout << "  future-sum test: expected=" << expected << " actual=" << total
              << (expected == total ? "  OK" : "  MISMATCH") << '\n';
    return expected == total;
}

}

int main() {
    unsigned hw = std::thread::hardware_concurrency();
    size_t numWorkers = hw == 0 ? 4 : hw;

    std::cout << "Lock-free MPMC queue + thread pool stress test\n"
              << "  hardware_concurrency: " << hw << " (using " << numWorkers << " workers)\n";

    bool allOk = true;

    std::cout << "Run 1: atomic-counter contention test\n";
    allOk &= runCounterStressTest(8, 50000, numWorkers);

    std::cout << "Run 2: repeat for consistency\n";
    allOk &= runCounterStressTest(16, 25000, numWorkers);

    std::cout << "Run 3: future-based result aggregation test\n";
    allOk &= runFutureSumTest(20000, numWorkers);

    std::cout << (allOk ? "All tests passed.\n" : "SOME TESTS FAILED.\n");
    return allOk ? EXIT_SUCCESS : EXIT_FAILURE;
}
