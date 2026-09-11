#ifndef GC_OBJECT_MODEL_HPP
#define GC_OBJECT_MODEL_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gc {

using Ref = std::uint32_t;

constexpr Ref kNull = 0;
constexpr Ref kHeapStart = 8;
constexpr Ref kOldSpaceBit = 0x80000000u;

constexpr std::uint8_t kMarkBit = 1;
constexpr std::uint8_t kForwardedBit = 2;

constexpr std::uint32_t kFreeTag = 0xFFFFFFFFu;

struct ObjectHeader {
    std::uint32_t size;
    std::uint32_t typeTag;
    std::uint32_t fieldCount;
    std::uint32_t payloadSize;
    std::uint32_t forward;
    std::uint8_t flags;
    std::uint8_t age;
    std::uint16_t padding;
};

inline std::uint32_t objectSize(std::uint32_t fieldCount, std::uint32_t payloadSize) {
    const std::size_t raw =
        sizeof(ObjectHeader) + static_cast<std::size_t>(fieldCount) * sizeof(Ref) + payloadSize;
    return static_cast<std::uint32_t>((raw + 7) & ~static_cast<std::size_t>(7));
}

inline std::uint32_t minimumBlockSize() { return objectSize(0, 0); }

inline bool isOldRef(Ref r) { return (r & kOldSpaceBit) != 0; }

inline Ref offsetOf(Ref r) { return r & ~kOldSpaceBit; }

inline Ref taggedOld(Ref offset) { return offset | kOldSpaceBit; }

inline void storeU64(unsigned char* payload, std::size_t offset, std::uint64_t value) {
    std::memcpy(payload + offset, &value, sizeof(value));
}

inline std::uint64_t loadU64(const unsigned char* payload, std::size_t offset) {
    std::uint64_t value = 0;
    std::memcpy(&value, payload + offset, sizeof(value));
    return value;
}

inline void storeBytes(unsigned char* payload, const std::string& text) {
    if (!text.empty()) {
        std::memcpy(payload, text.data(), text.size());
    }
}

inline std::string loadBytes(const unsigned char* payload, std::size_t length) {
    return std::string(reinterpret_cast<const char*>(payload), length);
}

class Space {
public:
    explicit Space(std::size_t capacity) : bytes_(alignCapacity(capacity), 0), top_(kHeapStart) {}

    std::size_t capacity() const { return bytes_.size(); }

    std::size_t top() const { return top_; }

    void setTop(std::size_t value) { top_ = value; }

    void resetTop() { top_ = kHeapStart; }

    std::size_t used() const { return top_ - kHeapStart; }

    std::size_t remaining() const { return bytes_.size() - top_; }

    bool holds(Ref r) const { return r >= kHeapStart && r < bytes_.size(); }

    ObjectHeader* header(Ref r) {
        return reinterpret_cast<ObjectHeader*>(bytes_.data() + offsetOf(r));
    }

    const ObjectHeader* header(Ref r) const {
        return reinterpret_cast<const ObjectHeader*>(bytes_.data() + offsetOf(r));
    }

    Ref* fields(Ref r) {
        return reinterpret_cast<Ref*>(bytes_.data() + offsetOf(r) + sizeof(ObjectHeader));
    }

    const Ref* fields(Ref r) const {
        return reinterpret_cast<const Ref*>(bytes_.data() + offsetOf(r) + sizeof(ObjectHeader));
    }

    unsigned char* payload(Ref r) {
        return bytes_.data() + offsetOf(r) + sizeof(ObjectHeader) +
               header(r)->fieldCount * sizeof(Ref);
    }

    const unsigned char* payload(Ref r) const {
        return bytes_.data() + offsetOf(r) + sizeof(ObjectHeader) +
               header(r)->fieldCount * sizeof(Ref);
    }

    Ref bumpAllocate(std::uint32_t typeTag, std::uint32_t fieldCount, std::uint32_t payloadSize) {
        const std::uint32_t need = objectSize(fieldCount, payloadSize);
        if (need > remaining()) {
            return kNull;
        }
        const Ref result = static_cast<Ref>(top_);
        top_ += need;
        initialize(result, need, typeTag, fieldCount, payloadSize);
        return result;
    }

    void initialize(Ref r, std::uint32_t size, std::uint32_t typeTag, std::uint32_t fieldCount,
                    std::uint32_t payloadSize) {
        std::memset(bytes_.data() + offsetOf(r), 0, size);
        ObjectHeader* h = header(r);
        h->size = size;
        h->typeTag = typeTag;
        h->fieldCount = fieldCount;
        h->payloadSize = payloadSize;
    }

    void copyBody(Ref destination, const Space& source, Ref origin) {
        const ObjectHeader* src = source.header(origin);
        const std::size_t bodyBytes =
            static_cast<std::size_t>(src->fieldCount) * sizeof(Ref) + src->payloadSize;
        std::memcpy(bytes_.data() + offsetOf(destination) + sizeof(ObjectHeader),
                    source.bytes_.data() + offsetOf(origin) + sizeof(ObjectHeader), bodyBytes);
    }

    void clear() {
        std::memset(bytes_.data(), 0, bytes_.size());
        top_ = kHeapStart;
    }

private:
    static std::size_t alignCapacity(std::size_t capacity) {
        const std::size_t floor = kHeapStart + minimumBlockSize();
        const std::size_t chosen = capacity < floor ? floor : capacity;
        return (chosen + 7) & ~static_cast<std::size_t>(7);
    }

    std::vector<unsigned char> bytes_;
    std::size_t top_;
};

class RootSet {
public:
    std::size_t add(Ref r) {
        if (!recycled_.empty()) {
            const std::size_t slot = recycled_.back();
            recycled_.pop_back();
            slots_[slot] = r;
            occupied_[slot] = 1;
            ++live_;
            return slot;
        }
        slots_.push_back(r);
        occupied_.push_back(1);
        ++live_;
        return slots_.size() - 1;
    }

    void remove(std::size_t slot) {
        if (slot >= slots_.size() || occupied_[slot] == 0) {
            return;
        }
        occupied_[slot] = 0;
        slots_[slot] = kNull;
        recycled_.push_back(slot);
        --live_;
    }

    Ref get(std::size_t slot) const { return slot < slots_.size() ? slots_[slot] : kNull; }

    void set(std::size_t slot, Ref r) {
        if (slot < slots_.size() && occupied_[slot] != 0) {
            slots_[slot] = r;
        }
    }

    std::size_t size() const { return live_; }

    template <typename Visitor>
    void forEach(Visitor visitor) {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (occupied_[i] != 0 && slots_[i] != kNull) {
                visitor(slots_[i]);
            }
        }
    }

    template <typename Visitor>
    void forEach(Visitor visitor) const {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (occupied_[i] != 0 && slots_[i] != kNull) {
                visitor(slots_[i]);
            }
        }
    }

private:
    std::vector<Ref> slots_;
    std::vector<std::uint8_t> occupied_;
    std::vector<std::size_t> recycled_;
    std::size_t live_ = 0;
};

class Handle {
public:
    Handle() : roots_(nullptr), slot_(0) {}

    Handle(RootSet& roots, Ref r) : roots_(&roots), slot_(roots.add(r)) {}

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : roots_(other.roots_), slot_(other.slot_) {
        other.roots_ = nullptr;
    }

    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            release();
            roots_ = other.roots_;
            slot_ = other.slot_;
            other.roots_ = nullptr;
        }
        return *this;
    }

    ~Handle() { release(); }

    Ref get() const { return roots_ != nullptr ? roots_->get(slot_) : kNull; }

    void set(Ref r) {
        if (roots_ != nullptr) {
            roots_->set(slot_, r);
        }
    }

    bool valid() const { return roots_ != nullptr; }

    void release() {
        if (roots_ != nullptr) {
            roots_->remove(slot_);
            roots_ = nullptr;
        }
    }

private:
    RootSet* roots_;
    std::size_t slot_;
};

struct SweepResult {
    std::size_t liveObjects = 0;
    std::size_t liveBytes = 0;
    std::size_t freedObjects = 0;
    std::size_t freedBytes = 0;
};

class FreeListSpace {
public:
    explicit FreeListSpace(std::size_t capacity) : space_(capacity), rover_(kHeapStart) {
        space_.setTop(space_.capacity());
        reset();
    }

    Space& space() { return space_; }

    const Space& space() const { return space_; }

    std::size_t capacity() const { return space_.capacity() - kHeapStart; }

    void reset() {
        space_.clear();
        space_.setTop(space_.capacity());
        ObjectHeader* h = space_.header(kHeapStart);
        h->size = static_cast<std::uint32_t>(space_.capacity() - kHeapStart);
        h->typeTag = kFreeTag;
        rover_ = kHeapStart;
    }

    Ref allocate(std::uint32_t typeTag, std::uint32_t fieldCount, std::uint32_t payloadSize) {
        const std::uint32_t need = objectSize(fieldCount, payloadSize);
        Ref result = scanRange(rover_, static_cast<Ref>(space_.capacity()), need);
        if (result == kNull) {
            result = scanRange(kHeapStart, rover_, need);
        }
        if (result == kNull) {
            return kNull;
        }
        carve(result, need, typeTag, fieldCount, payloadSize);
        return result;
    }

    template <typename IsLive>
    SweepResult sweep(IsLive isLive) {
        SweepResult result;
        Ref cursor = kHeapStart;
        Ref runStart = kNull;
        std::uint32_t runSize = 0;
        const Ref limit = static_cast<Ref>(space_.capacity());
        while (cursor < limit) {
            ObjectHeader* h = space_.header(cursor);
            const std::uint32_t size = h->size;
            const bool free = h->typeTag == kFreeTag;
            const bool live = !free && isLive(cursor);
            if (live) {
                if (runStart != kNull) {
                    makeFree(runStart, runSize);
                    runStart = kNull;
                    runSize = 0;
                }
                ++result.liveObjects;
                result.liveBytes += size;
            } else {
                if (!free) {
                    ++result.freedObjects;
                    result.freedBytes += size;
                }
                if (runStart == kNull) {
                    runStart = cursor;
                    runSize = 0;
                }
                runSize += size;
            }
            cursor += size;
        }
        if (runStart != kNull) {
            makeFree(runStart, runSize);
        }
        rover_ = kHeapStart;
        return result;
    }

    std::vector<Ref> objects() const {
        std::vector<Ref> result;
        Ref cursor = kHeapStart;
        const Ref limit = static_cast<Ref>(space_.capacity());
        while (cursor < limit) {
            const ObjectHeader* h = space_.header(cursor);
            if (h->typeTag != kFreeTag) {
                result.push_back(cursor);
            }
            cursor += h->size;
        }
        return result;
    }

    std::size_t usedBytes() const {
        std::size_t used = 0;
        Ref cursor = kHeapStart;
        const Ref limit = static_cast<Ref>(space_.capacity());
        while (cursor < limit) {
            const ObjectHeader* h = space_.header(cursor);
            if (h->typeTag != kFreeTag) {
                used += h->size;
            }
            cursor += h->size;
        }
        return used;
    }

    std::size_t freeBytes() const { return capacity() - usedBytes(); }

    std::size_t largestFreeBlock() const {
        std::size_t largest = 0;
        Ref cursor = kHeapStart;
        const Ref limit = static_cast<Ref>(space_.capacity());
        std::size_t run = 0;
        while (cursor < limit) {
            const ObjectHeader* h = space_.header(cursor);
            if (h->typeTag == kFreeTag) {
                run += h->size;
                if (run > largest) {
                    largest = run;
                }
            } else {
                run = 0;
            }
            cursor += h->size;
        }
        return largest;
    }

    std::size_t freeBlockCount() const {
        std::size_t count = 0;
        Ref cursor = kHeapStart;
        const Ref limit = static_cast<Ref>(space_.capacity());
        bool inRun = false;
        while (cursor < limit) {
            const ObjectHeader* h = space_.header(cursor);
            if (h->typeTag == kFreeTag) {
                if (!inRun) {
                    ++count;
                    inRun = true;
                }
            } else {
                inRun = false;
            }
            cursor += h->size;
        }
        return count;
    }

    bool validate() const {
        Ref cursor = kHeapStart;
        const Ref limit = static_cast<Ref>(space_.capacity());
        const std::uint32_t minimum = minimumBlockSize();
        while (cursor < limit) {
            const ObjectHeader* h = space_.header(cursor);
            if (h->size < minimum || (h->size % 8) != 0) {
                return false;
            }
            if (h->typeTag != kFreeTag) {
                const std::uint32_t needed = objectSize(h->fieldCount, h->payloadSize);
                if (needed > h->size) {
                    return false;
                }
            }
            cursor += h->size;
        }
        return cursor == limit;
    }

private:
    Ref scanRange(Ref from, Ref limit, std::uint32_t need) {
        Ref cursor = from;
        const Ref capacity = static_cast<Ref>(space_.capacity());
        while (cursor < limit) {
            ObjectHeader* h = space_.header(cursor);
            if (h->typeTag == kFreeTag) {
                std::uint32_t total = h->size;
                Ref next = cursor + total;
                while (next < capacity && space_.header(next)->typeTag == kFreeTag) {
                    total += space_.header(next)->size;
                    next = cursor + total;
                }
                h->size = total;
                if (total >= need) {
                    return cursor;
                }
                cursor = next;
            } else {
                cursor += h->size;
            }
        }
        return kNull;
    }

    void carve(Ref block, std::uint32_t need, std::uint32_t typeTag, std::uint32_t fieldCount,
               std::uint32_t payloadSize) {
        const std::uint32_t blockSize = space_.header(block)->size;
        std::uint32_t taken = need;
        const std::uint32_t remainder = blockSize - need;
        if (remainder >= minimumBlockSize()) {
            makeFree(block + need, remainder);
        } else {
            taken = blockSize;
        }
        space_.initialize(block, taken, typeTag, fieldCount, payloadSize);
        rover_ = block + taken;
        if (rover_ >= space_.capacity()) {
            rover_ = kHeapStart;
        }
    }

    void makeFree(Ref block, std::uint32_t size) {
        ObjectHeader* h = space_.header(block);
        h->size = size;
        h->typeTag = kFreeTag;
        h->fieldCount = 0;
        h->payloadSize = 0;
        h->forward = 0;
        h->flags = 0;
        h->age = 0;
    }

    Space space_;
    Ref rover_;
};

}

#endif
