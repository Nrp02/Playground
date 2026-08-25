#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../src/connection_pool.hpp"

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

connpool::ConnectionPool::Factory makeFactory() {
    return [](int id) { return std::make_unique<connpool::SimulatedConnection>(id); };
}

}

int main() {
    using namespace std::chrono_literals;

    {
        connpool::ConnectionPool pool(2, makeFactory());
        auto a = pool.acquire(100ms);
        auto b = pool.acquire(100ms);
        expectTrue(a.valid(), "first acquire on empty pool succeeds");
        expectTrue(b.valid(), "second acquire succeeds up to max size");
        expectTrue(a->id() != b->id(), "two acquired handles hold distinct connections");
        expectEq<std::size_t>(pool.createdCount(), 2, "created count reaches max size");
        expectEq<std::size_t>(pool.availableCount(), 0, "no idle connections while both checked out");
    }

    {
        connpool::ConnectionPool pool(1, makeFactory());
        int firstId = -1;
        {
            auto a = pool.acquire(100ms);
            expectTrue(a.valid(), "acquire succeeds for pool of size 1");
            firstId = a->id();
        }
        expectEq<std::size_t>(pool.availableCount(), 1, "released handle returns connection to pool");
        auto b = pool.acquire(100ms);
        expectTrue(b.valid(), "second acquire succeeds after release");
        expectEq(b->id(), firstId, "released connection is reused rather than recreated");
    }

    {
        connpool::ConnectionPool pool(1, makeFactory());
        auto held = pool.acquire(100ms);
        expectTrue(held.valid(), "pool grants the only connection");

        auto start = std::chrono::steady_clock::now();
        auto blocked = pool.acquire(120ms);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        expectTrue(!blocked.valid(), "acquire on exhausted pool times out rather than succeeding");
        expectTrue(elapsed >= 100ms, "timed-out acquire waited approximately the requested timeout");
    }

    {
        connpool::ConnectionPool pool(1, makeFactory());
        auto held = pool.acquire(100ms);
        expectTrue(held.valid(), "pool grants the only connection for release-unblocks test");

        std::thread releaser([&held] {
            std::this_thread::sleep_for(40ms);
            held = connpool::ConnectionPool::Handle();
        });

        auto start = std::chrono::steady_clock::now();
        auto second = pool.acquire(500ms);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        releaser.join();

        expectTrue(second.valid(), "blocked acquire succeeds once the connection is released");
        expectTrue(elapsed < 300ms, "blocked acquire returns promptly after release, not after full timeout");
    }

    {
        constexpr int kPoolSize = 4;
        constexpr int kThreads = 8;
        constexpr int kRoundsPerThread = 50;

        connpool::ConnectionPool pool(kPoolSize, makeFactory());
        std::mutex activeMutex;
        std::set<int> activeIds;
        std::atomic<bool> overlapDetected{false};
        std::atomic<int> successfulAcquires{0};

        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&] {
                for (int r = 0; r < kRoundsPerThread; ++r) {
                    auto handle = pool.acquire(1000ms);
                    if (!handle.valid()) {
                        continue;
                    }
                    int id = handle->id();
                    {
                        std::lock_guard<std::mutex> lock(activeMutex);
                        if (activeIds.count(id) > 0) {
                            overlapDetected.store(true);
                        }
                        activeIds.insert(id);
                    }
                    handle->execute();
                    std::this_thread::sleep_for(1ms);
                    {
                        std::lock_guard<std::mutex> lock(activeMutex);
                        activeIds.erase(id);
                    }
                    successfulAcquires.fetch_add(1);
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }

        expectTrue(!overlapDetected.load(), "concurrent stress test never hands the same connection to two holders at once");
        expectEq(successfulAcquires.load(), kThreads * kRoundsPerThread,
                 "concurrent stress test completes every acquire without deadlock or leak");
        expectTrue(pool.createdCount() <= static_cast<std::size_t>(kPoolSize),
                   "concurrent stress test never creates more connections than the max pool size");
    }

    {
        connpool::ConnectionPool pool(2, makeFactory());
        auto a = pool.acquire(100ms);
        int brokenId = a->id();
        a->markUnhealthy();
        a = connpool::ConnectionPool::Handle();

        expectEq<std::size_t>(pool.availableCount(), 1, "unhealthy connection is replaced, not discarded from the pool");
        auto b = pool.acquire(100ms);
        expectTrue(b.valid(), "acquiring after an unhealthy release succeeds");
        expectTrue(b->id() != brokenId, "unhealthy connection is never handed out again");
        expectTrue(b->isHealthy(), "replacement connection reports healthy");
    }

    {
        std::mutex brokenMutex;
        std::set<int> brokenIds;
        auto healthCheck = [&](connpool::SimulatedConnection& conn) {
            std::lock_guard<std::mutex> lock(brokenMutex);
            return brokenIds.count(conn.id()) == 0;
        };

        connpool::ConnectionPool pool(1, makeFactory(), healthCheck, 20ms);
        int idleId = -1;
        {
            auto a = pool.acquire(100ms);
            expectTrue(a.valid(), "acquire succeeds before background eviction test");
            idleId = a->id();
        }

        {
            std::lock_guard<std::mutex> lock(brokenMutex);
            brokenIds.insert(idleId);
        }

        std::this_thread::sleep_for(120ms);

        auto b = pool.acquire(100ms);
        expectTrue(b.valid(), "acquire succeeds after background health checker replaces the idle broken connection");
        expectTrue(b->id() != idleId, "background health checker evicted and replaced the idle unhealthy connection");
    }

    if (g_failures == 0) {
        std::cout << "\nall tests passed\n";
    } else {
        std::cout << "\n" << g_failures << " test(s) failed\n";
    }
    return g_failures == 0 ? 0 : 1;
}
