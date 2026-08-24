#pragma once

#include <cstddef>
#include <vector>

class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t block_size = 1 << 20);
    ~ArenaAllocator();

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    void* allocate(size_t size);
    void deallocate(void* ptr);
    void reset();

    size_t bytes_allocated() const { return total_allocated_; }
    size_t block_count() const { return blocks_.size(); }

private:
    struct Block {
        char* data;
        size_t capacity;
        size_t offset;
    };

    void add_block(size_t min_size);

    std::vector<Block> blocks_;
    size_t block_size_;
    size_t total_allocated_ = 0;
};
