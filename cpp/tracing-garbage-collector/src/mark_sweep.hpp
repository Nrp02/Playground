#ifndef GC_MARK_SWEEP_HPP
#define GC_MARK_SWEEP_HPP

#include <vector>

#include "object_model.hpp"

namespace gc {

struct MarkSweepStats {
    std::size_t collections = 0;
    std::size_t objectsAllocated = 0;
    std::size_t bytesAllocated = 0;
    std::size_t objectsCollected = 0;
    std::size_t bytesReclaimed = 0;
    std::size_t objectsMarked = 0;
    std::size_t liveBytesAfterLastCollection = 0;
    std::size_t failedAllocations = 0;
};

class MarkSweepHeap {
public:
    MarkSweepHeap(std::size_t capacity, RootSet& roots) : heap_(capacity), roots_(roots) {}

    Ref allocate(std::uint32_t typeTag, std::uint32_t fieldCount, std::uint32_t payloadSize) {
        Ref result = heap_.allocate(typeTag, fieldCount, payloadSize);
        if (result == kNull) {
            collect();
            result = heap_.allocate(typeTag, fieldCount, payloadSize);
        }
        if (result == kNull) {
            ++stats_.failedAllocations;
            return kNull;
        }
        ++stats_.objectsAllocated;
        stats_.bytesAllocated += heap_.space().header(result)->size;
        return result;
    }

    Ref tryAllocate(std::uint32_t typeTag, std::uint32_t fieldCount, std::uint32_t payloadSize) {
        const Ref result = heap_.allocate(typeTag, fieldCount, payloadSize);
        if (result != kNull) {
            ++stats_.objectsAllocated;
            stats_.bytesAllocated += heap_.space().header(result)->size;
        }
        return result;
    }

    void collect() {
        ++stats_.collections;
        mark();
        const SweepResult swept = heap_.sweep([this](Ref r) {
            ObjectHeader* h = heap_.space().header(r);
            const bool live = (h->flags & kMarkBit) != 0;
            h->flags = static_cast<std::uint8_t>(h->flags & ~kMarkBit);
            return live;
        });
        stats_.objectsCollected += swept.freedObjects;
        stats_.bytesReclaimed += swept.freedBytes;
        stats_.liveBytesAfterLastCollection = swept.liveBytes;
    }

    Ref field(Ref object, std::uint32_t index) const {
        return heap_.space().fields(object)[index];
    }

    void setField(Ref object, std::uint32_t index, Ref value) {
        heap_.space().fields(object)[index] = value;
    }

    unsigned char* payload(Ref object) { return heap_.space().payload(object); }

    const unsigned char* payload(Ref object) const { return heap_.space().payload(object); }

    const ObjectHeader* header(Ref object) const { return heap_.space().header(object); }

    std::vector<Ref> liveObjects() const { return heap_.objects(); }

    std::size_t usedBytes() const { return heap_.usedBytes(); }

    std::size_t freeBytes() const { return heap_.freeBytes(); }

    std::size_t capacity() const { return heap_.capacity(); }

    std::size_t largestFreeBlock() const { return heap_.largestFreeBlock(); }

    std::size_t freeBlockCount() const { return heap_.freeBlockCount(); }

    bool validate() const { return heap_.validate(); }

    const MarkSweepStats& stats() const { return stats_; }

    std::vector<Ref> reachable() const {
        std::vector<Ref> found;
        std::vector<Ref> stack;
        std::vector<Ref> seen;
        roots_.forEach([&stack](Ref r) { stack.push_back(r); });
        while (!stack.empty()) {
            const Ref current = stack.back();
            stack.pop_back();
            if (current == kNull) {
                continue;
            }
            bool known = false;
            for (Ref r : seen) {
                if (r == current) {
                    known = true;
                    break;
                }
            }
            if (known) {
                continue;
            }
            seen.push_back(current);
            found.push_back(current);
            const ObjectHeader* h = heap_.space().header(current);
            const Ref* slots = heap_.space().fields(current);
            for (std::uint32_t i = 0; i < h->fieldCount; ++i) {
                if (slots[i] != kNull) {
                    stack.push_back(slots[i]);
                }
            }
        }
        return found;
    }

private:
    void mark() {
        std::vector<Ref> stack;
        roots_.forEach([&stack](Ref r) {
            if (r != kNull) {
                stack.push_back(r);
            }
        });
        while (!stack.empty()) {
            const Ref current = stack.back();
            stack.pop_back();
            ObjectHeader* h = heap_.space().header(current);
            if ((h->flags & kMarkBit) != 0) {
                continue;
            }
            h->flags = static_cast<std::uint8_t>(h->flags | kMarkBit);
            ++stats_.objectsMarked;
            if (h->age < 255) {
                ++h->age;
            }
            const Ref* slots = heap_.space().fields(current);
            for (std::uint32_t i = 0; i < h->fieldCount; ++i) {
                if (slots[i] != kNull) {
                    stack.push_back(slots[i]);
                }
            }
        }
    }

    FreeListSpace heap_;
    RootSet& roots_;
    MarkSweepStats stats_;
};

}

#endif
