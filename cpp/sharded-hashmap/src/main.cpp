#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "sharded_hashmap.hpp"

int main() {
    std::cout << "=== concurrent insert/erase/read stress ===\n";
    {
        shm::ShardedHashMap<int, int> map(16);
        const int kInsertThreads = 4;
        const int kEraserThreads = 2;
        const int kReaderThreads = 4;
        const int kKeysPerThread = 20000;

        std::atomic<long> insertedCount{0};
        std::atomic<long> erasedCount{0};
        std::atomic<bool> stopReaders{false};

        std::vector<std::thread> writers;
        std::vector<std::thread> readers;

        for (int t = 0; t < kInsertThreads; ++t) {
            writers.emplace_back([&map, &insertedCount, t]() {
                int base = t * kKeysPerThread;
                for (int i = 0; i < kKeysPerThread; ++i) {
                    map.put(base + i, base + i);
                    insertedCount.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (int t = 0; t < kEraserThreads; ++t) {
            writers.emplace_back([&map, &erasedCount, t]() {
                int base = t * kKeysPerThread;
                for (int i = 0; i < kKeysPerThread; i += 2) {
                    for (int attempt = 0; attempt < 5; ++attempt) {
                        if (map.erase(base + i)) {
                            erasedCount.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                        std::this_thread::yield();
                    }
                }
            });
        }

        for (int t = 0; t < kReaderThreads; ++t) {
            readers.emplace_back([&map, &stopReaders]() {
                long seen = 0;
                int outValue = 0;
                while (!stopReaders.load(std::memory_order_relaxed)) {
                    map.get(seen % 80000, outValue);
                    ++seen;
                }
            });
        }

        for (auto& w : writers) {
            w.join();
        }
        stopReaders.store(true, std::memory_order_relaxed);
        for (auto& r : readers) {
            r.join();
        }

        long expected = insertedCount.load() - erasedCount.load();
        std::size_t actual = map.size();
        std::cout << "inserted=" << insertedCount.load() << " erased=" << erasedCount.load()
                   << " expected_size=" << expected << " actual_size=" << actual << "\n";
        std::cout << "invariant holds: " << std::boolalpha << (static_cast<long>(actual) == expected) << "\n";
    }

    std::cout << "\n=== concurrent counter increment (no lost updates) ===\n";
    {
        shm::ShardedHashMap<std::string, long> map(32);
        map.put("counter", 0);

        const int kThreads = 8;
        const int kIncrementsPerThread = 50000;

        std::vector<std::thread> workers;
        for (int t = 0; t < kThreads; ++t) {
            workers.emplace_back([&map]() {
                for (int i = 0; i < kIncrementsPerThread; ++i) {
                    map.update("counter", [](long& value) { ++value; });
                }
            });
        }
        for (auto& w : workers) {
            w.join();
        }

        auto finalValue = map.get("counter");
        long expected = static_cast<long>(kThreads) * kIncrementsPerThread;
        std::cout << "expected=" << expected << " actual=" << *finalValue << "\n";
        std::cout << "no lost updates: " << std::boolalpha << (*finalValue == expected) << "\n";
    }

    std::cout << "\n=== shard distribution for 100000 integer keys across 16 shards ===\n";
    {
        shm::ShardedHashMap<int, int> map(16);
        std::vector<std::size_t> counts(map.shardCount(), 0);
        for (int i = 0; i < 100000; ++i) {
            ++counts[map.shardIndexFor(i)];
        }
        for (std::size_t s = 0; s < counts.size(); ++s) {
            std::cout << "  shard " << s << ": " << counts[s] << " keys\n";
        }
    }

    return 0;
}
