#pragma once

#include <cstddef>
#include <chrono>
#include <string>

struct BenchResult {
    std::string label;
    double milliseconds;
    size_t node_count;
};

BenchResult bench_pool_linked_list(size_t node_count, size_t rounds);
BenchResult bench_pool_baseline_new_delete(size_t node_count, size_t rounds);

BenchResult bench_arena_variable_alloc(size_t alloc_count, size_t rounds);
BenchResult bench_arena_baseline_malloc(size_t alloc_count, size_t rounds);

void print_result(const BenchResult& r);
