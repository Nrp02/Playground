#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../src/slab_allocator.hpp"

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

bool is_aligned(void* ptr) {
    return (reinterpret_cast<std::uintptr_t>(ptr) % alignof(std::max_align_t)) == 0;
}

void testAlignmentAllClasses() {
    bool all_aligned = true;
    std::vector<void*> ptrs;
    for (std::size_t size : slaballoc::kClassSizes) {
        void* p = slaballoc::SlabAllocator::instance().allocate(size);
        if (!is_aligned(p)) {
            all_aligned = false;
        }
        ptrs.push_back(p);
    }
    for (std::size_t i = 0; i < ptrs.size(); ++i) {
        slaballoc::SlabAllocator::instance().deallocate(ptrs[i], slaballoc::kClassSizes[i]);
    }
    expectTrue(all_aligned, "all size classes produce max_align_t aligned pointers");
}

void testAlignmentBoundarySizes() {
    bool all_aligned = true;
    std::vector<std::pair<void*, std::size_t>> allocs;
    for (std::size_t size : slaballoc::kClassSizes) {
        for (std::size_t delta : {std::size_t{1}, std::size_t{0}}) {
            std::size_t s = (delta == 1 && size > 1) ? size - 1 : size;
            void* p = slaballoc::SlabAllocator::instance().allocate(s);
            if (!is_aligned(p)) {
                all_aligned = false;
            }
            allocs.push_back({p, s});
        }
    }
    void* above = slaballoc::SlabAllocator::instance().allocate(slaballoc::kLargeThreshold + 1);
    if (!is_aligned(above)) {
        all_aligned = false;
    }
    allocs.push_back({above, slaballoc::kLargeThreshold + 1});

    for (auto& item : allocs) {
        slaballoc::SlabAllocator::instance().deallocate(item.first, item.second);
    }
    expectTrue(all_aligned, "boundary sizes just below/at/above each class edge stay aligned");
}

void testNoOverlapAcrossLiveAllocations() {
    const int kCount = 500;
    std::vector<std::pair<void*, std::size_t>> allocs;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> size_dist(1, 5000);

    for (int i = 0; i < kCount; ++i) {
        std::size_t size = static_cast<std::size_t>(size_dist(rng));
        void* p = slaballoc::SlabAllocator::instance().allocate(size);
        unsigned char pattern = static_cast<unsigned char>(i & 0xFF);
        std::memset(p, pattern, size);
        allocs.push_back({p, size});
    }

    bool all_intact = true;
    for (int i = 0; i < kCount; ++i) {
        unsigned char pattern = static_cast<unsigned char>(i & 0xFF);
        unsigned char* bytes = static_cast<unsigned char*>(allocs[static_cast<std::size_t>(i)].first);
        std::size_t size = allocs[static_cast<std::size_t>(i)].second;
        for (std::size_t b = 0; b < size; ++b) {
            if (bytes[b] != pattern) {
                all_intact = false;
                break;
            }
        }
    }

    for (auto& item : allocs) {
        slaballoc::SlabAllocator::instance().deallocate(item.first, item.second);
    }

    expectTrue(all_intact, "distinct live allocations never overlap (pattern integrity)");
}

void testFreeThenReallocateReusesMemory() {
    const std::size_t size = 64;
    auto before = slaballoc::SlabAllocator::instance().stats();
    for (int round = 0; round < 100000; ++round) {
        void* p = slaballoc::SlabAllocator::instance().allocate(size);
        slaballoc::SlabAllocator::instance().deallocate(p, size);
    }
    auto after = slaballoc::SlabAllocator::instance().stats();
    int idx = -1;
    for (std::size_t i = 0; i < slaballoc::kNumClasses; ++i) {
        if (slaballoc::kClassSizes[i] == size) {
            idx = static_cast<int>(i);
            break;
        }
    }
    expectTrue(idx >= 0, "size class for 64 bytes exists");
    std::size_t growth =
        after.slabs_allocated[static_cast<std::size_t>(idx)] -
        before.slabs_allocated[static_cast<std::size_t>(idx)];
    expectTrue(growth <= 4, "long alloc/free loop does not grow slab count unboundedly");
}

void testZeroSizeAllocation() {
    void* p = slaballoc::SlabAllocator::instance().allocate(0);
    expectTrue(p != nullptr, "zero-size allocation returns a valid pointer");
    slaballoc::SlabAllocator::instance().deallocate(p, 0);
    expectTrue(true, "zero-size allocation can be freed without error");
}

void testLargeAllocation() {
    std::size_t size = slaballoc::kLargeThreshold * 4;
    void* p = slaballoc::SlabAllocator::instance().allocate(size);
    expectTrue(p != nullptr, "large above-threshold allocation succeeds");
    std::memset(p, 0xAB, size);
    unsigned char* bytes = static_cast<unsigned char*>(p);
    bool intact = true;
    for (std::size_t i = 0; i < size; ++i) {
        if (bytes[i] != 0xAB) {
            intact = false;
            break;
        }
    }
    expectTrue(intact, "large allocation is writable across its full extent");
    slaballoc::SlabAllocator::instance().deallocate(p, size);
}

void testMultiThreadedStressWithCrossThreadFree() {
    const int kThreads = 6;
    const int kOpsPerThread = 5000;
    std::atomic<int> total_failures{0};
    std::vector<void*> cross_thread_ptrs(static_cast<std::size_t>(kThreads), nullptr);
    std::vector<std::size_t> cross_thread_sizes(static_cast<std::size_t>(kThreads), 0);

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(static_cast<unsigned>(t) * 104729u + 3u);
            std::uniform_int_distribution<int> size_dist(1, 6000);
            for (int i = 0; i < kOpsPerThread; ++i) {
                std::size_t size = static_cast<std::size_t>(size_dist(rng));
                void* p = slaballoc::SlabAllocator::instance().allocate(size);
                unsigned char pattern = static_cast<unsigned char>((t * 13 + i) & 0xFF);
                std::memset(p, pattern, size);
                for (std::size_t b = 0; b < size; ++b) {
                    if (static_cast<unsigned char*>(p)[b] != pattern) {
                        total_failures.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                }
                slaballoc::SlabAllocator::instance().deallocate(p, size);
            }
            std::size_t handoff_size = 128;
            void* handoff = slaballoc::SlabAllocator::instance().allocate(handoff_size);
            std::memset(handoff, 0x77, handoff_size);
            cross_thread_ptrs[static_cast<std::size_t>(t)] = handoff;
            cross_thread_sizes[static_cast<std::size_t>(t)] = handoff_size;
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    bool cross_thread_ok = true;
    for (int t = 0; t < kThreads; ++t) {
        unsigned char* bytes = static_cast<unsigned char*>(cross_thread_ptrs[static_cast<std::size_t>(t)]);
        for (std::size_t b = 0; b < cross_thread_sizes[static_cast<std::size_t>(t)]; ++b) {
            if (bytes[b] != 0x77) {
                cross_thread_ok = false;
            }
        }
    }

    std::thread freer([&]() {
        for (int t = 0; t < kThreads; ++t) {
            slaballoc::SlabAllocator::instance().deallocate(
                cross_thread_ptrs[static_cast<std::size_t>(t)],
                cross_thread_sizes[static_cast<std::size_t>(t)]);
        }
    });
    freer.join();

    expectEq(total_failures.load(), 0, "multi-threaded randomized alloc/write/verify/free rounds are clean");
    expectTrue(cross_thread_ok, "cross-thread allocated memory retains its pattern before being freed by another thread");
}

void testStatisticsBalanceToZero() {
    auto before = slaballoc::SlabAllocator::instance().stats();
    std::vector<std::pair<void*, std::size_t>> allocs;
    std::mt19937 rng(999);
    std::uniform_int_distribution<int> size_dist(1, 9000);
    for (int i = 0; i < 300; ++i) {
        std::size_t size = static_cast<std::size_t>(size_dist(rng));
        void* p = slaballoc::SlabAllocator::instance().allocate(size);
        allocs.push_back({p, size});
    }
    for (auto& item : allocs) {
        slaballoc::SlabAllocator::instance().deallocate(item.first, item.second);
    }
    auto after = slaballoc::SlabAllocator::instance().stats();
    expectEq(after.bytes_in_use, before.bytes_in_use,
             "bytes in use returns to its prior baseline after everything is freed");
}

void testAllocatorAdapterWithVector() {
    std::vector<int, slaballoc::Allocator<int>> v;
    bool contents_ok = true;
    for (int i = 0; i < 20000; ++i) {
        v.push_back(i);
    }
    for (int i = 0; i < 20000; ++i) {
        if (v[static_cast<std::size_t>(i)] != i) {
            contents_ok = false;
            break;
        }
    }
    expectEq(v.size(), std::size_t{20000}, "Allocator<T>-backed vector has expected size");
    expectTrue(contents_ok, "Allocator<T>-backed vector retains correct contents across many reallocations");
}

}

int main() {
    testAlignmentAllClasses();
    testAlignmentBoundarySizes();
    testNoOverlapAcrossLiveAllocations();
    testFreeThenReallocateReusesMemory();
    testZeroSizeAllocation();
    testLargeAllocation();
    testMultiThreadedStressWithCrossThreadFree();
    testStatisticsBalanceToZero();
    testAllocatorAdapterWithVector();

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All tests passed\n";
    return 0;
}
