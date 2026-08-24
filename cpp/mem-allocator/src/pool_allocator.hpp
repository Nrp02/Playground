#pragma once

#include <cstddef>
#include <vector>

class PoolAllocator {
public:
    PoolAllocator(size_t block_size, size_t blocks_per_chunk = 4096);
    ~PoolAllocator();

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    void* allocate();
    void deallocate(void* ptr);

    size_t block_size() const { return block_size_; }
    size_t live_blocks() const { return live_blocks_; }
    size_t chunk_count() const { return chunks_.size(); }

private:
    struct FreeNode {
        FreeNode* next;
    };

    void add_chunk();

    size_t block_size_;
    size_t blocks_per_chunk_;
    std::vector<char*> chunks_;
    FreeNode* free_list_ = nullptr;
    size_t live_blocks_ = 0;
};
