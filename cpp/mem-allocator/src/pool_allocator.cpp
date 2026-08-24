#include "pool_allocator.hpp"

#include <cstdlib>
#include <algorithm>

PoolAllocator::PoolAllocator(size_t block_size, size_t blocks_per_chunk)
    : block_size_(std::max(block_size, sizeof(FreeNode))),
      blocks_per_chunk_(blocks_per_chunk) {
    add_chunk();
}

PoolAllocator::~PoolAllocator() {
    for (char* chunk : chunks_) {
        std::free(chunk);
    }
}

void PoolAllocator::add_chunk() {
    char* chunk = static_cast<char*>(std::malloc(block_size_ * blocks_per_chunk_));
    chunks_.push_back(chunk);

    for (size_t i = 0; i < blocks_per_chunk_; ++i) {
        char* block = chunk + i * block_size_;
        FreeNode* node = reinterpret_cast<FreeNode*>(block);
        node->next = free_list_;
        free_list_ = node;
    }
}

void* PoolAllocator::allocate() {
    if (free_list_ == nullptr) {
        add_chunk();
    }
    FreeNode* node = free_list_;
    free_list_ = node->next;
    ++live_blocks_;
    return node;
}

void PoolAllocator::deallocate(void* ptr) {
    if (ptr == nullptr) return;
    FreeNode* node = reinterpret_cast<FreeNode*>(ptr);
    node->next = free_list_;
    free_list_ = node;
    --live_blocks_;
}
