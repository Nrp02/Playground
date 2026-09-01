#include "slab_allocator.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <vector>

namespace {

void single_threaded_benchmark() {
    const std::size_t iterations = 200000;
    const std::size_t object_size = 64;

    auto slab_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        void* p = slaballoc::SlabAllocator::instance().allocate(object_size);
        slaballoc::SlabAllocator::instance().deallocate(p, object_size);
    }
    auto slab_end = std::chrono::steady_clock::now();

    auto new_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        void* p = ::operator new(object_size);
        ::operator delete(p, object_size);
    }
    auto new_end = std::chrono::steady_clock::now();

    double slab_ms =
        std::chrono::duration<double, std::milli>(slab_end - slab_start).count();
    double new_ms = std::chrono::duration<double, std::milli>(new_end - new_start).count();

    std::printf("=== Single-threaded alloc/free benchmark (%zu x %zu bytes) ===\n",
                iterations, object_size);
    std::printf("slab-allocator: %.3f ms\n", slab_ms);
    std::printf("new/delete:     %.3f ms\n", new_ms);

    auto stats = slaballoc::SlabAllocator::instance().stats();
    std::printf("cache hit rate after warm-up run: %.2f%%\n\n",
                stats.cache_hit_rate() * 100.0);
}

void multi_threaded_run() {
    const int thread_count = 4;
    const int ops_per_thread = 20000;

    std::mutex handoff_mutex;
    std::queue<std::pair<void*, std::size_t>> handoff_queue;
    std::atomic<int> handed_off{0};
    std::atomic<bool> producers_done{false};

    std::vector<std::thread> producers;
    for (int t = 0; t < thread_count; ++t) {
        producers.emplace_back([&, t]() {
            std::mt19937 rng(static_cast<unsigned>(t) * 7919u + 17u);
            std::uniform_int_distribution<int> size_dist(1, static_cast<int>(slaballoc::kClassSizes.back()));
            for (int i = 0; i < ops_per_thread; ++i) {
                std::size_t size = static_cast<std::size_t>(size_dist(rng));
                void* p = slaballoc::SlabAllocator::instance().allocate(size);
                unsigned char pattern = static_cast<unsigned char>((t * 31 + i) & 0xFF);
                std::memset(p, pattern, size);
                if (i % 5 == 0) {
                    std::lock_guard<std::mutex> lock(handoff_mutex);
                    handoff_queue.push({p, size});
                    handed_off.fetch_add(1, std::memory_order_relaxed);
                } else {
                    slaballoc::SlabAllocator::instance().deallocate(p, size);
                }
            }
        });
    }

    std::thread consumer([&]() {
        int consumed = 0;
        while (!producers_done.load(std::memory_order_relaxed) ||
               consumed < handed_off.load(std::memory_order_relaxed)) {
            std::pair<void*, std::size_t> item{nullptr, 0};
            {
                std::lock_guard<std::mutex> lock(handoff_mutex);
                if (!handoff_queue.empty()) {
                    item = handoff_queue.front();
                    handoff_queue.pop();
                }
            }
            if (item.first != nullptr) {
                slaballoc::SlabAllocator::instance().deallocate(item.first, item.second);
                ++consumed;
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (auto& th : producers) {
        th.join();
    }
    producers_done.store(true, std::memory_order_relaxed);
    consumer.join();

    std::printf("=== Multi-threaded run (%d threads x %d ops, with cross-thread frees) ===\n",
                thread_count, ops_per_thread);
    std::printf("objects handed off across threads: %d\n\n", handed_off.load());
}

void vector_with_slab_allocator() {
    std::vector<int, slaballoc::Allocator<int>> v;
    for (int i = 0; i < 5000; ++i) {
        v.push_back(i * i);
    }
    long long sum = 0;
    for (int value : v) {
        sum += value;
    }
    std::printf("=== std::vector backed by slaballoc::Allocator<int> ===\n");
    std::printf("elements: %zu, checksum: %lld\n", v.size(), sum);

    auto live_stats = slaballoc::SlabAllocator::instance().stats();
    std::printf("live bytes in use while vector is alive: %zu\n", live_stats.bytes_in_use);
    std::printf("overhead ratio while live (reserved/live): %.3f\n\n",
                live_stats.overhead_ratio());
}

}

int main() {
    single_threaded_benchmark();
    multi_threaded_run();
    vector_with_slab_allocator();

    auto stats = slaballoc::SlabAllocator::instance().stats();
    std::printf("=== Final statistics ===\n");
    std::printf("bytes in use: %zu\n", stats.bytes_in_use);
    std::printf("large allocations outstanding: %zu\n", stats.large_allocations);
    std::printf("cache hit rate: %.2f%%\n", stats.cache_hit_rate() * 100.0);
    std::printf("overhead ratio (reserved/live): %.3f\n", stats.overhead_ratio());
    for (std::size_t i = 0; i < slaballoc::kNumClasses; ++i) {
        if (stats.slabs_allocated[i] > 0) {
            std::printf("  class %4zu bytes: %zu slabs allocated\n", slaballoc::kClassSizes[i],
                        stats.slabs_allocated[i]);
        }
    }
    return 0;
}
