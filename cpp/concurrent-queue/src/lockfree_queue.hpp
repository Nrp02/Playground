#ifndef CONCURRENT_QUEUE_LOCKFREE_QUEUE_HPP
#define CONCURRENT_QUEUE_LOCKFREE_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <new>
#include <vector>

namespace cq {

template <typename T>
class MpmcBoundedQueue {
public:
    explicit MpmcBoundedQueue(size_t capacity)
        : capacity_(nextPowerOfTwo(capacity)), mask_(capacity_ - 1), buffer_(capacity_) {
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        enqueuePos_.store(0, std::memory_order_relaxed);
        dequeuePos_.store(0, std::memory_order_relaxed);
    }

    MpmcBoundedQueue(const MpmcBoundedQueue&) = delete;
    MpmcBoundedQueue& operator=(const MpmcBoundedQueue&) = delete;

    bool tryEnqueue(T item) {
        Cell* cell;
        size_t pos = enqueuePos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (enqueuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = enqueuePos_.load(std::memory_order_relaxed);
            }
        }
        cell->data = std::move(item);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool tryDequeue(T& item) {
        Cell* cell;
        size_t pos = dequeuePos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (diff == 0) {
                if (dequeuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = dequeuePos_.load(std::memory_order_relaxed);
            }
        }
        item = std::move(cell->data);
        cell->sequence.store(pos + mask_ + 1, std::memory_order_release);
        return true;
    }

    size_t capacity() const { return capacity_; }

private:
    struct Cell {
        std::atomic<size_t> sequence{0};
        T data{};
    };

    static size_t nextPowerOfTwo(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    size_t capacity_;
    size_t mask_;
    std::vector<Cell> buffer_;
    alignas(64) std::atomic<size_t> enqueuePos_;
    alignas(64) std::atomic<size_t> dequeuePos_;
};

}

#endif
