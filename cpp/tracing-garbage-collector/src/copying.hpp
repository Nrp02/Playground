#ifndef GC_COPYING_HPP
#define GC_COPYING_HPP

#include <algorithm>
#include <vector>

#include "object_model.hpp"

namespace gc {

struct CopyingStats {
    std::size_t collections = 0;
    std::size_t objectsAllocated = 0;
    std::size_t bytesAllocated = 0;
    std::size_t objectsCopied = 0;
    std::size_t bytesCopied = 0;
    std::size_t objectsCollected = 0;
    std::size_t bytesReclaimed = 0;
    std::size_t liveBytesAfterLastCollection = 0;
    std::size_t failedAllocations = 0;
};

class CopyingHeap {
public:
    CopyingHeap(std::size_t semiSpaceCapacity, RootSet& roots)
        : fromSpace_(semiSpaceCapacity), toSpace_(semiSpaceCapacity), roots_(roots) {}

    Ref allocate(std::uint32_t typeTag, std::uint32_t fieldCount, std::uint32_t payloadSize) {
        Ref result = fromSpace_.bumpAllocate(typeTag, fieldCount, payloadSize);
        if (result == kNull) {
            collect();
            result = fromSpace_.bumpAllocate(typeTag, fieldCount, payloadSize);
        }
        if (result == kNull) {
            ++stats_.failedAllocations;
            return kNull;
        }
        ++stats_.objectsAllocated;
        stats_.bytesAllocated += fromSpace_.header(result)->size;
        return result;
    }

    void collect() {
        ++stats_.collections;
        const std::size_t before = fromSpace_.used();
        const std::size_t objectsBefore = liveObjects().size();
        const std::size_t copiedBefore = stats_.objectsCopied;
        toSpace_.resetTop();
        roots_.forEach([this](Ref& r) { r = evacuate(r); });
        std::size_t scan = kHeapStart;
        while (scan < toSpace_.top()) {
            const Ref current = static_cast<Ref>(scan);
            ObjectHeader* h = toSpace_.header(current);
            const std::uint32_t fieldCount = h->fieldCount;
            const std::uint32_t size = h->size;
            for (std::uint32_t i = 0; i < fieldCount; ++i) {
                Ref* slots = toSpace_.fields(current);
                slots[i] = evacuate(slots[i]);
            }
            scan += size;
        }
        const std::size_t after = toSpace_.used();
        std::swap(fromSpace_, toSpace_);
        toSpace_.clear();
        stats_.bytesReclaimed += before - after;
        stats_.objectsCollected += objectsBefore - (stats_.objectsCopied - copiedBefore);
        stats_.liveBytesAfterLastCollection = after;
    }

    Ref field(Ref object, std::uint32_t index) const { return fromSpace_.fields(object)[index]; }

    void setField(Ref object, std::uint32_t index, Ref value) {
        fromSpace_.fields(object)[index] = value;
    }

    unsigned char* payload(Ref object) { return fromSpace_.payload(object); }

    const unsigned char* payload(Ref object) const { return fromSpace_.payload(object); }

    const ObjectHeader* header(Ref object) const { return fromSpace_.header(object); }

    std::size_t usedBytes() const { return fromSpace_.used(); }

    std::size_t freeBytes() const { return fromSpace_.remaining(); }

    std::size_t semiSpaceCapacity() const { return fromSpace_.capacity() - kHeapStart; }

    std::vector<Ref> liveObjects() const {
        std::vector<Ref> result;
        std::size_t cursor = kHeapStart;
        while (cursor < fromSpace_.top()) {
            const Ref current = static_cast<Ref>(cursor);
            result.push_back(current);
            cursor += fromSpace_.header(current)->size;
        }
        return result;
    }

    bool validate() const {
        std::size_t cursor = kHeapStart;
        while (cursor < fromSpace_.top()) {
            const ObjectHeader* h = fromSpace_.header(static_cast<Ref>(cursor));
            if (h->size < minimumBlockSize() || (h->size % 8) != 0) {
                return false;
            }
            if (objectSize(h->fieldCount, h->payloadSize) != h->size) {
                return false;
            }
            if ((h->flags & kForwardedBit) != 0) {
                return false;
            }
            cursor += h->size;
        }
        return cursor == fromSpace_.top();
    }

    const CopyingStats& stats() const { return stats_; }

private:
    Ref evacuate(Ref r) {
        if (r == kNull) {
            return kNull;
        }
        ObjectHeader* source = fromSpace_.header(r);
        if ((source->flags & kForwardedBit) != 0) {
            return source->forward;
        }
        const Ref destination =
            toSpace_.bumpAllocate(source->typeTag, source->fieldCount, source->payloadSize);
        if (destination == kNull) {
            ++stats_.failedAllocations;
            return kNull;
        }
        toSpace_.copyBody(destination, fromSpace_, r);
        ObjectHeader* target = toSpace_.header(destination);
        target->age = source->age < 255 ? static_cast<std::uint8_t>(source->age + 1) : source->age;
        source->flags = static_cast<std::uint8_t>(source->flags | kForwardedBit);
        source->forward = destination;
        ++stats_.objectsCopied;
        stats_.bytesCopied += target->size;
        return destination;
    }

    Space fromSpace_;
    Space toSpace_;
    RootSet& roots_;
    CopyingStats stats_;
};

}

#endif
