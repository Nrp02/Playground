#include "benchmark.hpp"

#include <cstdio>

int main() {
    const size_t node_count = 20000;
    const size_t alloc_count = 20000;
    const size_t rounds = 50;

    std::printf("=== Fixed-size workload: linked list (%zu nodes x %zu rounds) ===\n",
                node_count, rounds);
    print_result(bench_pool_linked_list(node_count, rounds));
    print_result(bench_pool_baseline_new_delete(node_count, rounds));

    std::printf("\n=== Variable-size workload: build/teardown (%zu allocs x %zu rounds) ===\n",
                alloc_count, rounds);
    print_result(bench_arena_variable_alloc(alloc_count, rounds));
    print_result(bench_arena_baseline_malloc(alloc_count, rounds));

    return 0;
}
