#ifndef GC_GENERATIONAL_HPP
#define GC_GENERATIONAL_HPP

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "object_model.hpp"

namespace gc {

struct GenerationalStats {
    std::size_t minorCollections = 0;
    std::size_t majorCollections = 0;
    std::size_t objectsAllocated = 0;
    std::size_t bytesAllocated = 0;
    std::size_t objectsCopied = 0;
    std::size_t bytesCopied = 0;
    std::size_t objectsPromoted = 0;
    std::size_t bytesPromoted = 0;
    std::size_t youngObjectsCollected = 0;
    std::size_t youngBytesReclaimed = 0;
    std::size_t oldObjectsCollected = 0;
    std::size_t oldBytesReclaimed = 0;
    std::size_t barrierWrites = 0;
    std::size_t rememberedAdditions = 0;
    std::size_t largeObjectAllocations = 0;
    std::size_t failedAllocations = 0;
};

class GenerationalHeap {
public:
    GenerationalHeap(std::size_t youngCapacity, std::size_t oldCapacity, RootSet& roots,
                     std::uint8_t promotionAge = 2)
        : young_(youngCapacity),
          spare_(youngCapacity),
          old_(oldCapacity),
          roots_(roots),
          promotionAge_(promotionAge == 0 ? 1 : promotionAge),
          largeObjectThreshold_(static_cast<std::uint32_t>((young_.capacity() - kHeapStart) / 4)) {}

    Ref allocate(std::uint32_t typeTag, std::uint32_t fieldCount, std::uint32_t payloadSize) {
        const std::uint32_t need = objectSize(fieldCount, payloadSize);
        if (need >= largeObjectThreshold_) {
            const Ref direct = allocateOld(typeTag, fieldCount, payloadSize);
            if (direct != kNull) {
                ++stats_.largeObjectAllocations;
                ++stats_.objectsAllocated;
                stats_.bytesAllocated += need;
                return direct;
            }
        }
        Ref result = young_.bumpAllocate(typeTag, fieldCount, payloadSize);
        if (result == kNull) {
            minorCollect();
            result = young_.bumpAllocate(typeTag, fieldCount, payloadSize);
        }
        if (result == kNull) {
            majorCollect();
            result = young_.bumpAllocate(typeTag, fieldCount, payloadSize);
        }
        if (result == kNull) {
            result = allocateOld(typeTag, fieldCount, payloadSize);
        }
        if (result == kNull) {
            ++stats_.failedAllocations;
            return kNull;
        }
        ++stats_.objectsAllocated;
        stats_.bytesAllocated += need;
        return result;
    }

    Ref field(Ref object, std::uint32_t index) const { return fieldsOf(object)[index]; }

    void setField(Ref object, std::uint32_t index, Ref value) {
        fieldsOf(object)[index] = value;
        ++stats_.barrierWrites;
        if (isOldRef(object) && value != kNull && !isOldRef(value)) {
            if (remembered_.insert(object).second) {
                ++stats_.rememberedAdditions;
            }
        }
    }

    unsigned char* payload(Ref object) {
        return isOldRef(object) ? old_.space().payload(object) : young_.payload(object);
    }

    const unsigned char* payload(Ref object) const {
        return isOldRef(object) ? old_.space().payload(object) : young_.payload(object);
    }

    const ObjectHeader* header(Ref object) const { return headerOf(object); }

    void minorCollect() { evacuateYoung(false); }

    void majorCollect() {
        evacuateYoung(true);
        collectOld();
    }

    std::size_t youngUsedBytes() const { return young_.used(); }

    std::size_t youngCapacity() const { return young_.capacity() - kHeapStart; }

    std::size_t oldUsedBytes() const { return old_.usedBytes(); }

    std::size_t oldCapacity() const { return old_.capacity(); }

    std::size_t rememberedSetSize() const { return remembered_.size(); }

    std::uint8_t promotionAge() const { return promotionAge_; }

    std::vector<Ref> youngObjects() const {
        std::vector<Ref> result;
        std::size_t cursor = kHeapStart;
        while (cursor < young_.top()) {
            const Ref current = static_cast<Ref>(cursor);
            result.push_back(current);
            cursor += young_.header(current)->size;
        }
        return result;
    }

    std::vector<Ref> oldObjects() const {
        std::vector<Ref> raw = old_.objects();
        std::vector<Ref> result;
        result.reserve(raw.size());
        for (Ref r : raw) {
            result.push_back(taggedOld(r));
        }
        return result;
    }

    std::size_t liveObjectCount() const { return youngObjects().size() + old_.objects().size(); }

    bool validate() const {
        if (!old_.validate()) {
            return false;
        }
        std::size_t cursor = kHeapStart;
        while (cursor < young_.top()) {
            const ObjectHeader* h = young_.header(static_cast<Ref>(cursor));
            if (objectSize(h->fieldCount, h->payloadSize) != h->size) {
                return false;
            }
            if ((h->flags & kForwardedBit) != 0) {
                return false;
            }
            cursor += h->size;
        }
        if (cursor != young_.top()) {
            return false;
        }
        for (Ref r : remembered_) {
            if (!isOldRef(r)) {
                return false;
            }
        }
        return true;
    }

    const GenerationalStats& stats() const { return stats_; }

private:
    const ObjectHeader* headerOf(Ref r) const {
        return isOldRef(r) ? old_.space().header(r) : young_.header(r);
    }

    ObjectHeader* headerOf(Ref r) {
        return isOldRef(r) ? old_.space().header(r) : young_.header(r);
    }

    const Ref* fieldsOf(Ref r) const {
        return isOldRef(r) ? old_.space().fields(r) : young_.fields(r);
    }

    Ref* fieldsOf(Ref r) { return isOldRef(r) ? old_.space().fields(r) : young_.fields(r); }

    Ref allocateOld(std::uint32_t typeTag, std::uint32_t fieldCount, std::uint32_t payloadSize) {
        Ref offset = old_.allocate(typeTag, fieldCount, payloadSize);
        if (offset == kNull) {
            collectOld();
            offset = old_.allocate(typeTag, fieldCount, payloadSize);
        }
        return offset == kNull ? kNull : taggedOld(offset);
    }

    void evacuateYoung(bool promoteAll) {
        if (old_.freeBytes() < young_.used() + minimumBlockSize()) {
            collectOld();
        }
        promoteAll_ = promoteAll;
        if (!promoteAll) {
            ++stats_.minorCollections;
        }
        const std::size_t liveBefore = young_.used();
        const std::size_t objectsBefore = youngObjects().size();
        spare_.resetTop();
        scanQueue_.clear();
        const std::vector<Ref> previouslyRemembered(remembered_.begin(), remembered_.end());
        remembered_.clear();
        roots_.forEach([this](Ref& r) { r = evacuateRef(r); });
        for (Ref oldObject : previouslyRemembered) {
            refreshOldObject(oldObject);
        }
        std::size_t scan = kHeapStart;
        for (;;) {
            bool progress = false;
            while (scan < spare_.top()) {
                const Ref current = static_cast<Ref>(scan);
                const std::uint32_t size = spare_.header(current)->size;
                const std::uint32_t fieldCount = spare_.header(current)->fieldCount;
                for (std::uint32_t i = 0; i < fieldCount; ++i) {
                    Ref* slots = spare_.fields(current);
                    slots[i] = evacuateRef(slots[i]);
                }
                scan += size;
                progress = true;
            }
            if (!scanQueue_.empty()) {
                const Ref pending = scanQueue_.back();
                scanQueue_.pop_back();
                refreshOldObject(pending);
                progress = true;
            }
            if (!progress) {
                break;
            }
        }
        const std::size_t survived = spare_.used();
        const std::size_t objectsAfter = spare_.top() == kHeapStart ? 0 : countObjects(spare_);
        std::swap(young_, spare_);
        spare_.clear();
        stats_.youngBytesReclaimed += liveBefore > survived ? liveBefore - survived : 0;
        stats_.youngObjectsCollected +=
            objectsBefore > objectsAfter ? objectsBefore - objectsAfter : 0;
    }

    static std::size_t countObjects(const Space& space) {
        std::size_t count = 0;
        std::size_t cursor = kHeapStart;
        while (cursor < space.top()) {
            cursor += space.header(static_cast<Ref>(cursor))->size;
            ++count;
        }
        return count;
    }

    Ref evacuateRef(Ref r) {
        if (r == kNull || isOldRef(r)) {
            return r;
        }
        ObjectHeader* source = young_.header(r);
        if ((source->flags & kForwardedBit) != 0) {
            return source->forward;
        }
        const std::uint8_t newAge =
            source->age < 255 ? static_cast<std::uint8_t>(source->age + 1) : source->age;
        Ref destination = kNull;
        if (!promoteAll_ && newAge < promotionAge_) {
            destination =
                spare_.bumpAllocate(source->typeTag, source->fieldCount, source->payloadSize);
            if (destination != kNull) {
                spare_.copyBody(destination, young_, r);
                spare_.header(destination)->age = newAge;
                ++stats_.objectsCopied;
                stats_.bytesCopied += spare_.header(destination)->size;
            }
        }
        if (destination == kNull) {
            const Ref offset =
                old_.allocate(source->typeTag, source->fieldCount, source->payloadSize);
            if (offset == kNull) {
                ++stats_.failedAllocations;
                return kNull;
            }
            old_.space().copyBody(offset, young_, r);
            old_.space().header(offset)->age = newAge;
            destination = taggedOld(offset);
            ++stats_.objectsPromoted;
            stats_.bytesPromoted += old_.space().header(offset)->size;
            scanQueue_.push_back(destination);
        }
        source->flags = static_cast<std::uint8_t>(source->flags | kForwardedBit);
        source->forward = destination;
        return destination;
    }

    void refreshOldObject(Ref oldObject) {
        const std::uint32_t fieldCount = old_.space().header(oldObject)->fieldCount;
        bool pointsToYoung = false;
        for (std::uint32_t i = 0; i < fieldCount; ++i) {
            Ref* slots = old_.space().fields(oldObject);
            const Ref updated = evacuateRef(slots[i]);
            slots[i] = updated;
            if (updated != kNull && !isOldRef(updated)) {
                pointsToYoung = true;
            }
        }
        if (pointsToYoung) {
            remembered_.insert(oldObject);
        }
    }

    void collectOld() {
        ++stats_.majorCollections;
        markAll();
        for (auto it = remembered_.begin(); it != remembered_.end();) {
            if ((old_.space().header(*it)->flags & kMarkBit) == 0) {
                it = remembered_.erase(it);
            } else {
                ++it;
            }
        }
        const SweepResult swept = old_.sweep([this](Ref offset) {
            ObjectHeader* h = old_.space().header(offset);
            const bool live = (h->flags & kMarkBit) != 0;
            h->flags = static_cast<std::uint8_t>(h->flags & ~kMarkBit);
            return live;
        });
        clearYoungMarks();
        stats_.oldObjectsCollected += swept.freedObjects;
        stats_.oldBytesReclaimed += swept.freedBytes;
    }

    void markAll() {
        std::vector<Ref> stack;
        roots_.forEach([&stack](Ref r) {
            if (r != kNull) {
                stack.push_back(r);
            }
        });
        while (!stack.empty()) {
            const Ref current = stack.back();
            stack.pop_back();
            ObjectHeader* h = headerOf(current);
            if ((h->flags & kMarkBit) != 0) {
                continue;
            }
            h->flags = static_cast<std::uint8_t>(h->flags | kMarkBit);
            const std::uint32_t fieldCount = h->fieldCount;
            for (std::uint32_t i = 0; i < fieldCount; ++i) {
                const Ref child = fieldsOf(current)[i];
                if (child != kNull) {
                    stack.push_back(child);
                }
            }
        }
    }

    void clearYoungMarks() {
        std::size_t cursor = kHeapStart;
        while (cursor < young_.top()) {
            const Ref current = static_cast<Ref>(cursor);
            ObjectHeader* h = young_.header(current);
            h->flags = static_cast<std::uint8_t>(h->flags & ~kMarkBit);
            cursor += h->size;
        }
    }

    Space young_;
    Space spare_;
    FreeListSpace old_;
    RootSet& roots_;
    std::uint8_t promotionAge_;
    std::uint32_t largeObjectThreshold_;
    bool promoteAll_ = false;
    std::unordered_set<Ref> remembered_;
    std::vector<Ref> scanQueue_;
    GenerationalStats stats_;
};

}

#endif
