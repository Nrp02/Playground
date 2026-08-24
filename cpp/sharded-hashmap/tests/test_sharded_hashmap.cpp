#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "../src/sharded_hashmap.hpp"

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
        shm::ShardedHashMap<int, int> map(8);
        expectTrue(map.empty(), "new map is empty");
        expectEq<std::size_t>(map.size(), 0, "new map has size 0");
        expectTrue(!map.contains(1), "empty map contains nothing");
        int outValue = 0;
        expectTrue(!map.get(1, outValue), "get on empty map fails");
    }

    {
        shm::ShardedHashMap<int, std::string> map(8);
        map.put(1, "one");
        map.put(2, "two");
        expectEq<std::size_t>(map.size(), 2, "size reflects two puts");
        expectTrue(map.contains(1), "contains finds inserted key");
        expectTrue(map.contains(2), "contains finds second inserted key");
        expectTrue(!map.contains(3), "contains is false for missing key");

        std::string outValue;
        expectTrue(map.get(1, outValue), "get succeeds for existing key");
        expectEq<std::string>(outValue, "one", "get returns correct value");

        auto opt = map.get(2);
        expectTrue(opt.has_value(), "optional get returns a value");
        expectEq<std::string>(*opt, "two", "optional get returns correct value");

        auto missing = map.get(99);
        expectTrue(!missing.has_value(), "optional get returns nullopt for missing key");
    }

    {
        shm::ShardedHashMap<int, int> map(8);
        map.put(5, 100);
        map.put(5, 200);
        expectEq<std::size_t>(map.size(), 1, "put overwrites existing key without growing size");
        int outValue = 0;
        map.get(5, outValue);
        expectEq(outValue, 200, "put overwrites value");
    }

    {
        shm::ShardedHashMap<int, int> map(8);
        expectTrue(!map.erase(1), "erase on empty map fails");
        map.put(1, 10);
        expectTrue(map.erase(1), "erase existing key succeeds");
        expectTrue(!map.contains(1), "key gone after erase");
        expectTrue(!map.erase(1), "erase again fails");
        expectEq<std::size_t>(map.size(), 0, "size is 0 after erasing only key");
    }

    {
        shm::ShardedHashMap<int, int> map(4);
        for (int i = 0; i < 1000; ++i) {
            map.put(i, i * i);
        }
        expectEq<std::size_t>(map.size(), 1000, "size matches number of unique inserted keys");
        bool allCorrect = true;
        for (int i = 0; i < 1000; ++i) {
            int outValue = 0;
            if (!map.get(i, outValue) || outValue != i * i) {
                allCorrect = false;
                break;
            }
        }
        expectTrue(allCorrect, "all 1000 values retrievable and correct");

        for (int i = 0; i < 1000; i += 2) {
            map.erase(i);
        }
        expectEq<std::size_t>(map.size(), 500, "size halves after erasing even keys");
        expectTrue(!map.contains(0), "even key erased");
        expectTrue(map.contains(1), "odd key still present");
    }

    {
        shm::ShardedHashMap<int, int> map(16);
        expectTrue(!map.update(1, [](int& v) { v += 1; }), "update on missing key fails");
        map.put(1, 41);
        expectTrue(map.update(1, [](int& v) { v += 1; }), "update on existing key succeeds");
        int outValue = 0;
        map.get(1, outValue);
        expectEq(outValue, 42, "update mutates value in place");
    }

    {
        shm::ShardedHashMap<int, int> map(16);
        const int kThreads = 8;
        const int kKeysPerThread = 5000;
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&map, t]() {
                int base = t * kKeysPerThread;
                for (int i = 0; i < kKeysPerThread; ++i) {
                    map.put(base + i, base + i);
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        expectEq<std::size_t>(map.size(), static_cast<std::size_t>(kThreads * kKeysPerThread),
                               "concurrent disjoint inserts produce no lost writes");

        bool allCorrect = true;
        for (int t = 0; t < kThreads; ++t) {
            int base = t * kKeysPerThread;
            for (int i = 0; i < kKeysPerThread; ++i) {
                int outValue = 0;
                if (!map.get(base + i, outValue) || outValue != base + i) {
                    allCorrect = false;
                    break;
                }
            }
        }
        expectTrue(allCorrect, "all concurrently inserted values are correct");
    }

    {
        shm::ShardedHashMap<std::string, long> map(16);
        map.put("counter", 0);
        const int kThreads = 8;
        const int kIncrementsPerThread = 20000;
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&map]() {
                for (int i = 0; i < kIncrementsPerThread; ++i) {
                    map.update("counter", [](long& v) { ++v; });
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        auto finalValue = map.get("counter");
        expectTrue(finalValue.has_value(), "counter key still present after concurrent updates");
        expectEq<long>(*finalValue, static_cast<long>(kThreads) * kIncrementsPerThread,
                        "concurrent counter increments produce no lost updates");
    }

    {
        shm::ShardedHashMap<int, int> map(16);
        std::atomic<long> putCount{0};
        std::atomic<long> eraseCount{0};
        const int kThreads = 6;
        const int kOpsPerThread = 8000;
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&map, &putCount, &eraseCount, t]() {
                int base = t * kOpsPerThread;
                for (int i = 0; i < kOpsPerThread; ++i) {
                    map.put(base + i, base + i);
                    putCount.fetch_add(1, std::memory_order_relaxed);
                    if (i % 3 == 0) {
                        if (map.erase(base + i)) {
                            eraseCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        long expected = putCount.load() - eraseCount.load();
        expectEq<long>(static_cast<long>(map.size()), expected,
                        "concurrent put/erase mix keeps size consistent with net operation count");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
