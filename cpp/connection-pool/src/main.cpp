#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "connection_pool.hpp"

namespace {

std::mutex g_printMutex;

void log(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_printMutex);
    std::printf("%s\n", message.c_str());
}

}

int main() {
    using namespace std::chrono_literals;

    connpool::ConnectionPool pool(
        3,
        [](int id) {
            bool flaky = (id % 2 == 0);
            return std::make_unique<connpool::SimulatedConnection>(id, flaky);
        },
        connpool::ConnectionPool::defaultHealthCheck, 50ms);

    log("=== concurrent worker demo ===");

    std::atomic<int> completed{0};
    std::atomic<int> timedOut{0};
    std::atomic<int> maxObservedId{0};
    std::vector<std::thread> workers;

    for (int w = 0; w < 6; ++w) {
        workers.emplace_back([&pool, &completed, &timedOut, &maxObservedId, w] {
            for (int round = 0; round < 5; ++round) {
                auto handle = pool.acquire(200ms);
                if (!handle.valid()) {
                    timedOut.fetch_add(1);
                    log("worker " + std::to_string(w) + " timed out acquiring a connection");
                    continue;
                }
                int connId = handle->id();
                int previousMax = maxObservedId.load();
                while (connId > previousMax && !maxObservedId.compare_exchange_weak(previousMax, connId)) {
                }
                if (connId > 3) {
                    log("worker " + std::to_string(w) + " picked up freshly created connection " +
                        std::to_string(connId) + " (a flaky connection was evicted and replaced)");
                }
                handle->execute();
                log("worker " + std::to_string(w) + " used connection " + std::to_string(connId) +
                    " (execute count " + std::to_string(handle->executeCount()) + ")");
                std::this_thread::sleep_for(15ms);
                completed.fetch_add(1);
            }
        });
    }

    for (auto& t : workers) {
        t.join();
    }

    log("completed operations: " + std::to_string(completed.load()));
    log("timed out attempts: " + std::to_string(timedOut.load()));
    log("pool created connections (currently live): " + std::to_string(pool.createdCount()));
    log("pool available connections: " + std::to_string(pool.availableCount()));
    log("highest connection id ever created: " + std::to_string(maxObservedId.load()) +
        " (pool size is 3, so anything above that came from a flaky-connection replacement)");

    log("");
    log("=== blocking-with-timeout demo ===");

    connpool::ConnectionPool singlePool(1, [](int id) {
        return std::make_unique<connpool::SimulatedConnection>(id);
    });

    auto holder = singlePool.acquire(100ms);
    log("main thread holds the only connection (id " + std::to_string(holder->id()) + ")");

    std::thread releaser([&holder] {
        std::this_thread::sleep_for(80ms);
        holder = connpool::ConnectionPool::Handle();
    });

    auto start = std::chrono::steady_clock::now();
    auto second = singlePool.acquire(500ms);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    releaser.join();

    if (second.valid()) {
        log("second acquire succeeded after ~" + std::to_string(elapsed.count()) +
            "ms once the first connection was released");
    } else {
        log("second acquire timed out unexpectedly");
    }

    auto thirdStart = std::chrono::steady_clock::now();
    auto third = singlePool.acquire(100ms);
    auto thirdElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - thirdStart);
    if (!third.valid()) {
        log("third acquire correctly timed out after ~" + std::to_string(thirdElapsed.count()) +
            "ms while pool was still exhausted");
    } else {
        log("third acquire unexpectedly succeeded");
    }

    return 0;
}
