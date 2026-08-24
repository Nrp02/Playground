#include "benchmark.hpp"
#include "pool_allocator.hpp"
#include "arena_allocator.hpp"

#include <cstdlib>
#include <cstdio>
#include <random>
#include <vector>

namespace {

struct ListNode {
    int value;
    char payload[24];
    ListNode* next;
};

double elapsed_ms(std::chrono::steady_clock::time_point start,
                   std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

}

BenchResult bench_pool_linked_list(size_t node_count, size_t rounds) {
    PoolAllocator pool(sizeof(ListNode));
    auto start = std::chrono::steady_clock::now();

    for (size_t r = 0; r < rounds; ++r) {
        ListNode* head = nullptr;
        for (size_t i = 0; i < node_count; ++i) {
            ListNode* n = static_cast<ListNode*>(pool.allocate());
            n->value = static_cast<int>(i);
            n->next = head;
            head = n;
        }
        while (head != nullptr) {
            ListNode* next = head->next;
            pool.deallocate(head);
            head = next;
        }
    }

    auto end = std::chrono::steady_clock::now();
    return {"PoolAllocator linked-list", elapsed_ms(start, end), node_count * rounds};
}

BenchResult bench_pool_baseline_new_delete(size_t node_count, size_t rounds) {
    auto start = std::chrono::steady_clock::now();

    for (size_t r = 0; r < rounds; ++r) {
        ListNode* head = nullptr;
        for (size_t i = 0; i < node_count; ++i) {
            ListNode* n = new ListNode;
            n->value = static_cast<int>(i);
            n->next = head;
            head = n;
        }
        while (head != nullptr) {
            ListNode* next = head->next;
            delete head;
            head = next;
        }
    }

    auto end = std::chrono::steady_clock::now();
    return {"new/delete linked-list", elapsed_ms(start, end), node_count * rounds};
}

BenchResult bench_arena_variable_alloc(size_t alloc_count, size_t rounds) {
    ArenaAllocator arena;
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> size_dist(16, 256);

    std::vector<size_t> sizes(alloc_count);
    for (auto& s : sizes) s = size_dist(rng);

    auto start = std::chrono::steady_clock::now();

    for (size_t r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < alloc_count; ++i) {
            void* p = arena.allocate(sizes[i]);
            static_cast<char*>(p)[0] = static_cast<char>(i);
        }
        arena.reset();
    }

    auto end = std::chrono::steady_clock::now();
    return {"ArenaAllocator build/reset", elapsed_ms(start, end), alloc_count * rounds};
}

BenchResult bench_arena_baseline_malloc(size_t alloc_count, size_t rounds) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> size_dist(16, 256);

    std::vector<size_t> sizes(alloc_count);
    for (auto& s : sizes) s = size_dist(rng);

    std::vector<void*> ptrs(alloc_count);

    auto start = std::chrono::steady_clock::now();

    for (size_t r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < alloc_count; ++i) {
            ptrs[i] = std::malloc(sizes[i]);
            static_cast<char*>(ptrs[i])[0] = static_cast<char>(i);
        }
        for (size_t i = 0; i < alloc_count; ++i) {
            std::free(ptrs[i]);
        }
    }

    auto end = std::chrono::steady_clock::now();
    return {"malloc/free build/teardown", elapsed_ms(start, end), alloc_count * rounds};
}

void print_result(const BenchResult& r) {
    std::printf("%-30s %10.3f ms   (%zu allocations)\n",
                r.label.c_str(), r.milliseconds, r.node_count);
}
