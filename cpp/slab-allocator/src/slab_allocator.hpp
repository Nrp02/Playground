#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

namespace slaballoc {

inline constexpr std::array<std::size_t, 17> kClassSizes = {
    8, 16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096};
inline constexpr std::size_t kNumClasses = kClassSizes.size();
inline constexpr std::size_t kLargeThreshold = 4096;
inline constexpr std::size_t kSlabSize = 65536;
inline constexpr std::size_t kCacheBatchSize = 32;
inline constexpr std::size_t kCacheHighWatermark = 64;
inline constexpr std::size_t kEmptySlabRetentionCap = 2;

struct FreeNode {
    FreeNode* next;
};

struct SlabHeader {
    SlabHeader* prev = nullptr;
    SlabHeader* next = nullptr;
    FreeNode* free_list = nullptr;
    std::uint32_t class_index = 0;
    std::uint32_t used = 0;
    std::uint32_t capacity = 0;
    std::uint32_t stride = 0;
};

class SlabAllocator;

struct ThreadCache {
    struct PerClass {
        FreeNode* head = nullptr;
        std::size_t count = 0;
    };
    std::array<PerClass, kNumClasses> classes;
    ~ThreadCache();
};

namespace detail {

constexpr std::size_t round_up(std::size_t value, std::size_t align) {
    return (value + align - 1) / align * align;
}

inline std::size_t stride_for_class(std::size_t index) {
    std::size_t raw = kClassSizes[index];
    if (raw < sizeof(void*)) {
        raw = sizeof(void*);
    }
    return round_up(raw, alignof(std::max_align_t));
}

inline void list_push_front(SlabHeader*& head, SlabHeader* node) {
    node->prev = nullptr;
    node->next = head;
    if (head != nullptr) {
        head->prev = node;
    }
    head = node;
}

inline void list_remove(SlabHeader*& head, SlabHeader* node) {
    if (node->prev != nullptr) {
        node->prev->next = node->next;
    } else {
        head = node->next;
    }
    if (node->next != nullptr) {
        node->next->prev = node->prev;
    }
    node->prev = nullptr;
    node->next = nullptr;
}

inline SlabHeader* create_slab(std::uint32_t class_index) {
    void* raw = ::operator new(kSlabSize, std::align_val_t(kSlabSize));
    SlabHeader* header = new (raw) SlabHeader();
    header->class_index = class_index;
    header->stride = static_cast<std::uint32_t>(stride_for_class(class_index));
    std::size_t objects_start = round_up(sizeof(SlabHeader), alignof(std::max_align_t));
    header->capacity =
        static_cast<std::uint32_t>((kSlabSize - objects_start) / header->stride);
    header->used = 0;
    char* base = reinterpret_cast<char*>(raw) + objects_start;
    FreeNode* prev_node = nullptr;
    for (std::uint32_t i = 0; i < header->capacity; ++i) {
        FreeNode* node = reinterpret_cast<FreeNode*>(base + static_cast<std::size_t>(i) *
                                                                 header->stride);
        node->next = prev_node;
        prev_node = node;
    }
    header->free_list = prev_node;
    return header;
}

inline void destroy_slab(SlabHeader* slab) {
    slab->~SlabHeader();
    ::operator delete(slab, kSlabSize, std::align_val_t(kSlabSize));
}

inline std::array<int, kLargeThreshold / 8 + 1> build_class_table() {
    std::array<int, kLargeThreshold / 8 + 1> table{};
    for (std::size_t granule = 0; granule < table.size(); ++granule) {
        std::size_t need = granule * 8;
        int found = -1;
        for (std::size_t i = 0; i < kNumClasses; ++i) {
            if (kClassSizes[i] >= need) {
                found = static_cast<int>(i);
                break;
            }
        }
        table[granule] = found;
    }
    return table;
}

inline int class_index_for_size(std::size_t size) {
    static const std::array<int, kLargeThreshold / 8 + 1> table = build_class_table();
    std::size_t granule = (size + 7) / 8;
    if (granule >= table.size()) {
        return -1;
    }
    return table[granule];
}

}

struct CentralClass {
    std::mutex mutex;
    SlabHeader* partial_head = nullptr;
    SlabHeader* full_head = nullptr;
    SlabHeader* empty_head = nullptr;
    std::size_t empty_count = 0;
    std::size_t slabs_allocated = 0;
};

class SlabAllocator {
public:
    struct Stats {
        std::size_t bytes_in_use = 0;
        std::size_t cache_hits = 0;
        std::size_t central_hits = 0;
        std::size_t large_allocations = 0;
        std::size_t large_bytes_in_use = 0;
        std::array<std::size_t, kNumClasses> slabs_allocated{};

        double cache_hit_rate() const {
            std::size_t total = cache_hits + central_hits;
            if (total == 0) {
                return 0.0;
            }
            return static_cast<double>(cache_hits) / static_cast<double>(total);
        }

        double overhead_ratio() const {
            if (bytes_in_use == 0) {
                return 0.0;
            }
            std::size_t slab_bytes = 0;
            for (std::size_t i = 0; i < kNumClasses; ++i) {
                slab_bytes += slabs_allocated[i] * kSlabSize;
            }
            std::size_t reserved = slab_bytes + large_bytes_in_use;
            return static_cast<double>(reserved) / static_cast<double>(bytes_in_use);
        }
    };

    static SlabAllocator& instance();

    void* allocate(std::size_t size);
    void deallocate(void* ptr, std::size_t size);
    Stats stats() const;

    std::size_t central_refill(std::uint32_t class_idx, std::size_t want, FreeNode*& out_head);
    void central_flush(std::uint32_t class_idx, FreeNode* head, std::size_t count);

private:
    SlabAllocator();
    SlabAllocator(const SlabAllocator&) = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;

    std::vector<std::unique_ptr<CentralClass>> central_;
    std::atomic<std::size_t> bytes_in_use_{0};
    std::atomic<std::size_t> cache_hits_{0};
    std::atomic<std::size_t> central_hits_{0};
    std::atomic<std::size_t> large_allocations_{0};
    std::atomic<std::size_t> large_bytes_in_use_{0};
};

inline SlabAllocator::SlabAllocator() {
    central_.reserve(kNumClasses);
    for (std::size_t i = 0; i < kNumClasses; ++i) {
        central_.push_back(std::make_unique<CentralClass>());
    }
}

inline SlabAllocator& SlabAllocator::instance() {
    static SlabAllocator inst;
    return inst;
}

inline std::size_t SlabAllocator::central_refill(std::uint32_t class_idx, std::size_t want,
                                                  FreeNode*& out_head) {
    CentralClass& c = *central_[class_idx];
    FreeNode* head = nullptr;
    FreeNode* tail = nullptr;
    std::size_t collected = 0;
    std::lock_guard<std::mutex> lock(c.mutex);
    while (collected < want) {
        SlabHeader* slab = c.partial_head;
        if (slab == nullptr) {
            if (c.empty_head != nullptr) {
                slab = c.empty_head;
                detail::list_remove(c.empty_head, slab);
                --c.empty_count;
                detail::list_push_front(c.partial_head, slab);
            } else {
                slab = detail::create_slab(class_idx);
                ++c.slabs_allocated;
                detail::list_push_front(c.partial_head, slab);
            }
        }
        while (slab->free_list != nullptr && collected < want) {
            FreeNode* node = slab->free_list;
            slab->free_list = node->next;
            ++slab->used;
            node->next = nullptr;
            if (head == nullptr) {
                head = node;
                tail = node;
            } else {
                tail->next = node;
                tail = node;
            }
            ++collected;
        }
        if (slab->used == slab->capacity) {
            detail::list_remove(c.partial_head, slab);
            detail::list_push_front(c.full_head, slab);
        }
    }
    out_head = head;
    return collected;
}

inline void SlabAllocator::central_flush(std::uint32_t class_idx, FreeNode* head,
                                          std::size_t count) {
    (void)count;
    CentralClass& c = *central_[class_idx];
    std::lock_guard<std::mutex> lock(c.mutex);
    FreeNode* node = head;
    while (node != nullptr) {
        FreeNode* next = node->next;
        std::uintptr_t base = reinterpret_cast<std::uintptr_t>(node) &
                               ~(static_cast<std::uintptr_t>(kSlabSize) - 1);
        SlabHeader* slab = reinterpret_cast<SlabHeader*>(base);
        bool was_full = (slab->used == slab->capacity);
        node->next = slab->free_list;
        slab->free_list = node;
        --slab->used;
        if (was_full) {
            detail::list_remove(c.full_head, slab);
            detail::list_push_front(c.partial_head, slab);
        }
        if (slab->used == 0) {
            detail::list_remove(c.partial_head, slab);
            if (c.empty_count < kEmptySlabRetentionCap) {
                detail::list_push_front(c.empty_head, slab);
                ++c.empty_count;
            } else {
                detail::destroy_slab(slab);
            }
        }
        node = next;
    }
}

inline SlabAllocator::Stats SlabAllocator::stats() const {
    Stats s;
    s.bytes_in_use = bytes_in_use_.load(std::memory_order_relaxed);
    s.cache_hits = cache_hits_.load(std::memory_order_relaxed);
    s.central_hits = central_hits_.load(std::memory_order_relaxed);
    s.large_allocations = large_allocations_.load(std::memory_order_relaxed);
    s.large_bytes_in_use = large_bytes_in_use_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < kNumClasses; ++i) {
        std::lock_guard<std::mutex> lock(central_[i]->mutex);
        s.slabs_allocated[i] = central_[i]->slabs_allocated;
    }
    return s;
}

inline ThreadCache& get_thread_cache() {
    thread_local ThreadCache cache;
    return cache;
}

inline ThreadCache::~ThreadCache() {
    for (std::size_t i = 0; i < kNumClasses; ++i) {
        PerClass& pc = classes[i];
        if (pc.head != nullptr) {
            SlabAllocator::instance().central_flush(static_cast<std::uint32_t>(i), pc.head,
                                                     pc.count);
            pc.head = nullptr;
            pc.count = 0;
        }
    }
}

inline void* SlabAllocator::allocate(std::size_t size) {
    int idx = detail::class_index_for_size(size);
    if (idx < 0) {
        void* p = ::operator new(size);
        large_allocations_.fetch_add(1, std::memory_order_relaxed);
        large_bytes_in_use_.fetch_add(size, std::memory_order_relaxed);
        bytes_in_use_.fetch_add(size, std::memory_order_relaxed);
        return p;
    }
    ThreadCache& cache = get_thread_cache();
    ThreadCache::PerClass& pc = cache.classes[static_cast<std::size_t>(idx)];
    if (pc.head == nullptr) {
        FreeNode* refilled = nullptr;
        std::size_t got =
            central_refill(static_cast<std::uint32_t>(idx), kCacheBatchSize, refilled);
        pc.head = refilled;
        pc.count = got;
        central_hits_.fetch_add(1, std::memory_order_relaxed);
    } else {
        cache_hits_.fetch_add(1, std::memory_order_relaxed);
    }
    FreeNode* node = pc.head;
    pc.head = node->next;
    --pc.count;
    bytes_in_use_.fetch_add(kClassSizes[static_cast<std::size_t>(idx)],
                             std::memory_order_relaxed);
    return node;
}

inline void SlabAllocator::deallocate(void* ptr, std::size_t size) {
    if (ptr == nullptr) {
        return;
    }
    int idx = detail::class_index_for_size(size);
    if (idx < 0) {
        ::operator delete(ptr, size);
        large_allocations_.fetch_sub(1, std::memory_order_relaxed);
        large_bytes_in_use_.fetch_sub(size, std::memory_order_relaxed);
        bytes_in_use_.fetch_sub(size, std::memory_order_relaxed);
        return;
    }
    ThreadCache& cache = get_thread_cache();
    ThreadCache::PerClass& pc = cache.classes[static_cast<std::size_t>(idx)];
    FreeNode* node = reinterpret_cast<FreeNode*>(ptr);
    node->next = pc.head;
    pc.head = node;
    ++pc.count;
    bytes_in_use_.fetch_sub(kClassSizes[static_cast<std::size_t>(idx)],
                             std::memory_order_relaxed);
    if (pc.count > kCacheHighWatermark) {
        std::size_t flush_count = pc.count / 2;
        FreeNode* flush_head = pc.head;
        FreeNode* keep_walker = flush_head;
        for (std::size_t i = 1; i < flush_count; ++i) {
            keep_walker = keep_walker->next;
        }
        FreeNode* new_head = keep_walker->next;
        keep_walker->next = nullptr;
        central_flush(static_cast<std::uint32_t>(idx), flush_head, flush_count);
        pc.head = new_head;
        pc.count -= flush_count;
    }
}

template <typename T>
struct Allocator {
    using value_type = T;

    Allocator() noexcept = default;

    template <typename U>
    Allocator(const Allocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        void* p = SlabAllocator::instance().allocate(n * sizeof(T));
        return static_cast<T*>(p);
    }

    void deallocate(T* p, std::size_t n) noexcept {
        SlabAllocator::instance().deallocate(p, n * sizeof(T));
    }
};

template <typename T, typename U>
bool operator==(const Allocator<T>&, const Allocator<U>&) noexcept {
    return true;
}

template <typename T, typename U>
bool operator!=(const Allocator<T>&, const Allocator<U>&) noexcept {
    return false;
}

}
