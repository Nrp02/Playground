#include "arena_allocator.hpp"

#include <cstdlib>
#include <algorithm>

namespace {
constexpr size_t kAlignment = 16;

size_t align_up(size_t n) {
    return (n + kAlignment - 1) & ~(kAlignment - 1);
}
}

ArenaAllocator::ArenaAllocator(size_t block_size) : block_size_(block_size) {
    add_block(block_size_);
}

ArenaAllocator::~ArenaAllocator() {
    for (auto& b : blocks_) {
        std::free(b.data);
    }
}

void ArenaAllocator::add_block(size_t min_size) {
    size_t cap = std::max(min_size, block_size_);
    Block b;
    b.data = static_cast<char*>(std::malloc(cap));
    b.capacity = cap;
    b.offset = 0;
    blocks_.push_back(b);
}

void* ArenaAllocator::allocate(size_t size) {
    size_t aligned = align_up(size);
    Block& back = blocks_.back();
    if (back.offset + aligned > back.capacity) {
        add_block(aligned);
    }
    Block& active = blocks_.back();
    void* ptr = active.data + active.offset;
    active.offset += aligned;
    total_allocated_ += aligned;
    return ptr;
}

void ArenaAllocator::deallocate(void*) {
}

void ArenaAllocator::reset() {
    for (auto& b : blocks_) {
        b.offset = 0;
    }
    total_allocated_ = 0;
}
