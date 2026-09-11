#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../src/copying.hpp"
#include "../src/generational.hpp"
#include "../src/mark_sweep.hpp"

namespace {

int g_failures = 0;

void expectTrue(bool condition, const std::string& testName) {
    if (!condition) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

template <typename T>
void expectEq(const T& actual, const T& expected, const std::string& testName) {
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

constexpr std::uint32_t kTagNode = 1;
constexpr std::uint32_t kTagBlob = 2;

template <typename Heap>
gc::Ref makeNode(Heap& heap, std::uint64_t value, std::uint32_t fields = 1) {
    const gc::Ref node = heap.allocate(kTagNode, fields, 8);
    if (node != gc::kNull) {
        gc::storeU64(heap.payload(node), 0, value);
    }
    return node;
}

template <typename Heap>
std::uint64_t nodeValue(Heap& heap, gc::Ref node) {
    return gc::loadU64(heap.payload(node), 0);
}

void objectModelTests() {
    expectEq(gc::objectSize(0, 0) % 8, 0u, "empty object size is eight byte aligned");
    expectEq(gc::objectSize(3, 5), gc::objectSize(3, 8), "payload rounds up to the alignment");
    expectTrue(gc::objectSize(4, 0) > gc::objectSize(0, 0), "fields grow the object");
    expectTrue(gc::isOldRef(gc::taggedOld(64)), "tagged refs report the old generation");
    expectEq(gc::offsetOf(gc::taggedOld(64)), static_cast<gc::Ref>(64),
             "tagging is reversible");
    expectTrue(!gc::isOldRef(64), "plain refs report the young generation");

    gc::Space space(1024);
    const gc::Ref first = space.bumpAllocate(kTagNode, 2, 16);
    const gc::Ref second = space.bumpAllocate(kTagBlob, 0, 32);
    expectTrue(first != gc::kNull && second != gc::kNull, "bump allocation succeeds");
    expectTrue(second > first, "bump allocation moves forward");
    expectEq(space.header(first)->typeTag, kTagNode, "type tag is stored");
    expectEq(space.header(first)->fieldCount, 2u, "field count is stored");
    expectEq(space.fields(first)[0], gc::kNull, "fields start out null");

    gc::storeU64(space.payload(first), 0, 0xDEADBEEFull);
    gc::storeU64(space.payload(first), 8, 7);
    expectEq(gc::loadU64(space.payload(first), 0), 0xDEADBEEFull, "payload roundtrips");
    expectEq(gc::loadU64(space.payload(first), 8), static_cast<std::uint64_t>(7),
             "second payload slot roundtrips");

    gc::storeBytes(space.payload(second), "garbage collection");
    expectEq(gc::loadBytes(space.payload(second), 18), std::string("garbage collection"),
             "byte payload roundtrips");

    const std::size_t used = space.used();
    expectTrue(used == space.header(first)->size + space.header(second)->size,
               "used bytes equal the sum of object sizes");
    expectTrue(space.bumpAllocate(kTagBlob, 0, 4096) == gc::kNull,
               "oversized allocation fails cleanly");
    space.resetTop();
    expectEq(space.used(), static_cast<std::size_t>(0), "reset empties the space");
}

void rootSetTests() {
    gc::RootSet roots;
    const std::size_t a = roots.add(16);
    const std::size_t b = roots.add(32);
    expectEq(roots.size(), static_cast<std::size_t>(2), "root set counts live slots");
    expectEq(roots.get(a), static_cast<gc::Ref>(16), "root slot holds its ref");
    roots.remove(a);
    expectEq(roots.size(), static_cast<std::size_t>(1), "removing a root shrinks the set");
    const std::size_t c = roots.add(48);
    expectEq(c, a, "removed slots are recycled");

    std::vector<gc::Ref> seen;
    roots.forEach([&seen](gc::Ref r) { seen.push_back(r); });
    std::sort(seen.begin(), seen.end());
    expectEq(seen, std::vector<gc::Ref>({32, 48}), "iteration visits only live roots");

    roots.forEach([](gc::Ref& r) { r = r + 1; });
    expectEq(roots.get(b), static_cast<gc::Ref>(33), "mutable iteration rewrites roots");

    {
        gc::Handle handle(roots, 100);
        expectEq(roots.size(), static_cast<std::size_t>(3), "a handle registers a root");
        handle.set(101);
        expectEq(handle.get(), static_cast<gc::Ref>(101), "a handle reads back what it wrote");
        gc::Handle moved(std::move(handle));
        expectTrue(!handle.valid(), "a moved from handle is empty");
        expectEq(moved.get(), static_cast<gc::Ref>(101), "the moved handle keeps the ref");
        expectEq(roots.size(), static_cast<std::size_t>(3), "moving does not add a root");
    }
    expectEq(roots.size(), static_cast<std::size_t>(2), "handles release their slot on scope exit");
}

void freeListTests() {
    gc::FreeListSpace heap(4096);
    expectTrue(heap.validate(), "a fresh free list heap is well formed");
    expectEq(heap.usedBytes(), static_cast<std::size_t>(0), "a fresh heap is empty");
    expectEq(heap.freeBlockCount(), static_cast<std::size_t>(1), "a fresh heap has one free block");

    std::vector<gc::Ref> blocks;
    bool allocated = true;
    for (int i = 0; i < 30; ++i) {
        const gc::Ref r = heap.allocate(kTagBlob, 0, 32);
        allocated = allocated && r != gc::kNull;
        blocks.push_back(r);
    }
    expectTrue(allocated, "free list allocation succeeds");
    expectTrue(heap.validate(), "the heap stays well formed after allocations");
    expectTrue(heap.usedBytes() >= 30 * gc::objectSize(0, 32), "used bytes cover every block");

    std::set<gc::Ref> keep;
    for (std::size_t i = 1; i < blocks.size(); i += 2) {
        keep.insert(blocks[i]);
    }
    const gc::SweepResult swept = heap.sweep([&keep](gc::Ref r) { return keep.count(r) != 0; });
    expectEq(swept.liveObjects, keep.size(), "sweep keeps exactly the live objects");
    expectEq(swept.freedObjects, blocks.size() - keep.size(), "sweep reports the freed objects");
    expectTrue(heap.validate(), "the heap stays well formed after a sweep");
    expectEq(heap.objects().size(), keep.size(), "walking the heap finds the survivors");

    const std::size_t fragmentedLargest = heap.largestFreeBlock();
    heap.sweep([](gc::Ref) { return false; });
    expectEq(heap.usedBytes(), static_cast<std::size_t>(0), "sweeping everything empties the heap");
    expectEq(heap.freeBlockCount(), static_cast<std::size_t>(1),
             "adjacent free blocks coalesce into one");
    expectTrue(heap.largestFreeBlock() > fragmentedLargest,
               "coalescing recovers a larger contiguous block");
    expectEq(heap.largestFreeBlock(), heap.capacity(), "the coalesced block spans the heap");

    expectTrue(heap.allocate(kTagBlob, 0, 8192) == gc::kNull,
               "an allocation larger than the heap fails");
    expectTrue(heap.validate(), "a failed allocation leaves the heap well formed");
}

void markSweepTests() {
    gc::RootSet roots;
    gc::MarkSweepHeap heap(8192, roots);

    gc::Handle head(roots, gc::kNull);
    for (std::uint64_t i = 0; i < 10; ++i) {
        const gc::Ref node = makeNode(heap, i);
        heap.setField(node, 0, head.get());
        head.set(node);
    }
    const gc::Ref garbageA = makeNode(heap, 100);
    const gc::Ref garbageB = makeNode(heap, 101);
    heap.setField(garbageA, 0, garbageB);
    heap.setField(garbageB, 0, garbageA);

    expectEq(heap.liveObjects().size(), static_cast<std::size_t>(12), "all objects are allocated");
    heap.collect();
    expectEq(heap.liveObjects().size(), static_cast<std::size_t>(10),
             "an unreachable cycle is collected");
    expectEq(heap.stats().objectsCollected, static_cast<std::size_t>(2),
             "the collector reports two dead objects");
    expectTrue(heap.validate(), "the heap is well formed after collection");

    std::uint64_t sum = 0;
    std::size_t length = 0;
    for (gc::Ref cursor = head.get(); cursor != gc::kNull; cursor = heap.field(cursor, 0)) {
        sum += nodeValue(heap, cursor);
        ++length;
    }
    expectEq(length, static_cast<std::size_t>(10), "the reachable list keeps its length");
    expectEq(sum, static_cast<std::uint64_t>(45), "payloads survive a collection intact");

    const gc::Ref beforeAddress = head.get();
    heap.collect();
    expectEq(head.get(), beforeAddress, "mark-sweep never moves an object");

    heap.setField(head.get(), 0, gc::kNull);
    heap.collect();
    expectEq(heap.liveObjects().size(), static_cast<std::size_t>(1),
             "cutting the list collects its tail");

    head.release();
    heap.collect();
    expectEq(heap.liveObjects().size(), static_cast<std::size_t>(0),
             "dropping every root collects everything");
    expectEq(heap.usedBytes(), static_cast<std::size_t>(0), "an empty heap uses no bytes");
    expectEq(heap.freeBytes(), heap.capacity(), "an empty heap is entirely free");
}

void markSweepPressureTests() {
    gc::RootSet roots;
    gc::MarkSweepHeap heap(4096, roots);
    gc::Handle keep(roots, makeNode(heap, 1));

    bool allocated = true;
    for (int i = 0; i < 2000 && allocated; ++i) {
        allocated = makeNode(heap, static_cast<std::uint64_t>(i)) != gc::kNull;
    }
    expectTrue(allocated, "allocation under pressure succeeds");
    expectTrue(heap.stats().collections > 0, "exhausting the heap triggers a collection");
    expectEq(heap.stats().failedAllocations, static_cast<std::size_t>(0),
             "collecting keeps allocation from failing");
    expectEq(nodeValue(heap, keep.get()), static_cast<std::uint64_t>(1),
             "the rooted object survives every collection");
    expectTrue(heap.validate(), "the heap survives allocation pressure intact");
}

void markSweepRandomGraphTests() {
    gc::RootSet roots;
    gc::MarkSweepHeap heap(32768, roots);
    std::mt19937 rng(1234);

    std::vector<gc::Ref> nodes;
    std::vector<gc::Handle> handles;
    for (int i = 0; i < 120; ++i) {
        nodes.push_back(makeNode(heap, static_cast<std::uint64_t>(i), 3));
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (std::uint32_t f = 0; f < 3; ++f) {
            if (rng() % 3 == 0) {
                heap.setField(nodes[i], f, nodes[rng() % nodes.size()]);
            }
        }
    }
    for (int i = 0; i < 8; ++i) {
        handles.emplace_back(roots, nodes[rng() % nodes.size()]);
    }

    std::vector<gc::Ref> expected = heap.reachable();
    std::sort(expected.begin(), expected.end());
    heap.collect();
    std::vector<gc::Ref> actual = heap.liveObjects();
    std::sort(actual.begin(), actual.end());
    expectEq(actual, expected, "the survivors match an independent reachability walk");
    expectTrue(heap.validate(), "a random graph collection leaves the heap well formed");

    bool payloadsInRange = true;
    for (gc::Ref r : actual) {
        payloadsInRange = payloadsInRange && nodeValue(heap, r) < nodes.size();
    }
    expectTrue(payloadsInRange, "every survivor keeps a valid payload");

    handles.clear();
    heap.collect();
    expectEq(heap.liveObjects().size(), static_cast<std::size_t>(0),
             "the random graph is fully collected once unrooted");
}

void copyingTests() {
    gc::RootSet roots;
    gc::CopyingHeap heap(8192, roots);

    gc::Handle shared(roots, makeNode(heap, 7));
    gc::Handle left(roots, makeNode(heap, 8, 2));
    gc::Handle right(roots, makeNode(heap, 9, 2));
    heap.setField(left.get(), 0, shared.get());
    heap.setField(right.get(), 0, shared.get());
    heap.setField(left.get(), 1, right.get());

    for (int i = 0; i < 50; ++i) {
        makeNode(heap, 1000);
    }
    const std::size_t before = heap.usedBytes();
    heap.collect();

    expectTrue(heap.usedBytes() < before, "collection compacts the heap");
    expectEq(heap.liveObjects().size(), static_cast<std::size_t>(3),
             "only the reachable objects are copied");
    expectEq(heap.stats().objectsCopied, static_cast<std::size_t>(3),
             "the collector reports the copies it made");
    expectEq(nodeValue(heap, shared.get()), static_cast<std::uint64_t>(7),
             "a copied payload is preserved");
    expectEq(heap.field(left.get(), 0), heap.field(right.get(), 0),
             "shared structure is copied exactly once");
    expectEq(heap.field(left.get(), 1), right.get(), "back references are updated");
    expectTrue(heap.validate(), "the to-space is a contiguous run of objects");
    expectEq(heap.usedBytes(),
             static_cast<std::size_t>(heap.header(shared.get())->size +
                                      heap.header(left.get())->size +
                                      heap.header(right.get())->size),
             "used bytes equal the live bytes after compaction");

    const gc::Ref cycleA = makeNode(heap, 21, 1);
    const gc::Ref cycleB = makeNode(heap, 22, 1);
    heap.setField(cycleA, 0, cycleB);
    heap.setField(cycleB, 0, cycleA);
    gc::Handle cycle(roots, cycleA);
    heap.collect();
    expectEq(nodeValue(heap, cycle.get()), static_cast<std::uint64_t>(21),
             "a rooted cycle survives evacuation");
    expectEq(nodeValue(heap, heap.field(heap.field(cycle.get(), 0), 0)),
             static_cast<std::uint64_t>(21), "a cycle is not copied twice");
    expectEq(heap.field(heap.field(cycle.get(), 0), 0), cycle.get(),
             "the cycle closes back on the same copy");

    const std::uint8_t age = heap.header(shared.get())->age;
    heap.collect();
    expectTrue(heap.header(shared.get())->age > age, "surviving an evacuation ages an object");
}

void copyingPressureTests() {
    gc::RootSet roots;
    gc::CopyingHeap heap(16384, roots);
    gc::Handle chain(roots, gc::kNull);
    std::mt19937 rng(99);

    std::uint64_t expectedHead = 0;
    bool allocated = true;
    for (int i = 0; i < 3000 && allocated; ++i) {
        const gc::Ref node = makeNode(heap, static_cast<std::uint64_t>(i));
        allocated = node != gc::kNull;
        if (!allocated) {
            break;
        }
        heap.setField(node, 0, chain.get());
        chain.set(node);
        expectedHead = static_cast<std::uint64_t>(i);
        if (rng() % 20 == 0) {
            chain.set(gc::kNull);
        }
    }
    expectTrue(allocated, "allocation under evacuation pressure succeeds");
    expectTrue(heap.stats().collections > 3, "a churning workload collects repeatedly");
    expectEq(heap.stats().failedAllocations, static_cast<std::size_t>(0),
             "evacuation keeps allocation from failing");
    expectEq(nodeValue(heap, chain.get()), expectedHead, "the chain head survives every cycle");
    expectTrue(heap.validate(), "the heap is well formed after heavy churn");

    std::size_t length = 0;
    std::uint64_t previous = expectedHead + 1;
    bool ordered = true;
    for (gc::Ref cursor = chain.get(); cursor != gc::kNull; cursor = heap.field(cursor, 0)) {
        const std::uint64_t value = nodeValue(heap, cursor);
        ordered = ordered && value < previous;
        previous = value;
        ++length;
    }
    expectTrue(ordered, "chain values stay strictly decreasing");
    expectTrue(length > 0, "the surviving chain is walkable after compaction");
}

void generationalTests() {
    gc::RootSet roots;
    gc::GenerationalHeap heap(4096, 32768, roots, 2);

    gc::Handle survivor(roots, makeNode(heap, 5));
    expectTrue(!gc::isOldRef(survivor.get()), "a fresh object starts in the nursery");

    heap.minorCollect();
    expectTrue(!gc::isOldRef(survivor.get()), "one survival is not enough to promote");
    heap.minorCollect();
    expectTrue(gc::isOldRef(survivor.get()), "reaching the promotion age moves an object to the old generation");
    expectEq(nodeValue(heap, survivor.get()), static_cast<std::uint64_t>(5),
             "a promoted payload is preserved");
    expectEq(heap.stats().objectsPromoted, static_cast<std::size_t>(1),
             "the collector reports the promotion");
    expectEq(heap.youngObjects().size(), static_cast<std::size_t>(0),
             "the nursery is empty after promotion");

    const gc::Ref fresh = makeNode(heap, 61);
    expectEq(heap.rememberedSetSize(), static_cast<std::size_t>(0),
             "the remembered set starts empty");
    heap.setField(survivor.get(), 0, fresh);
    expectEq(heap.rememberedSetSize(), static_cast<std::size_t>(1),
             "the write barrier records an old to young reference");
    heap.setField(survivor.get(), 0, fresh);
    expectEq(heap.stats().rememberedAdditions, static_cast<std::size_t>(1),
             "the remembered set does not record duplicates");

    heap.minorCollect();
    const gc::Ref moved = heap.field(survivor.get(), 0);
    expectTrue(moved != gc::kNull, "an object held only by the old generation survives a minor collection");
    expectEq(nodeValue(heap, moved), static_cast<std::uint64_t>(61),
             "the remembered reference is updated to the new location");
    expectEq(heap.rememberedSetSize(), static_cast<std::size_t>(1),
             "the remembered set is rebuilt while the reference stays young");

    heap.setField(survivor.get(), 0, gc::kNull);
    heap.minorCollect();
    expectEq(heap.rememberedSetSize(), static_cast<std::size_t>(0),
             "clearing the reference drops the remembered entry");
    expectTrue(heap.validate(), "the heap is well formed after minor collections");
}

void generationalMajorTests() {
    gc::RootSet roots;
    gc::GenerationalHeap heap(4096, 32768, roots, 2);

    gc::Handle doomed(roots, gc::kNull);
    for (std::uint64_t i = 0; i < 25; ++i) {
        const gc::Ref node = makeNode(heap, i);
        heap.setField(node, 0, doomed.get());
        doomed.set(node);
    }
    gc::Handle keeper(roots, makeNode(heap, 999));

    heap.majorCollect();
    expectEq(heap.youngObjects().size(), static_cast<std::size_t>(0),
             "a major collection promotes every young survivor");
    expectTrue(gc::isOldRef(doomed.get()), "the chain head is promoted");
    expectEq(heap.oldObjects().size(), static_cast<std::size_t>(26),
             "every reachable object lands in the old generation");

    std::size_t length = 0;
    for (gc::Ref cursor = doomed.get(); cursor != gc::kNull; cursor = heap.field(cursor, 0)) {
        ++length;
    }
    expectEq(length, static_cast<std::size_t>(25), "the promoted chain is intact");

    const std::size_t oldBefore = heap.oldUsedBytes();
    doomed.release();
    heap.majorCollect();
    expectEq(heap.oldObjects().size(), static_cast<std::size_t>(1),
             "a major collection reclaims unreachable old objects");
    expectTrue(heap.oldUsedBytes() < oldBefore, "the old generation shrinks after a major collection");
    expectEq(heap.stats().oldObjectsCollected, static_cast<std::size_t>(25),
             "the collector reports the old objects it freed");
    expectEq(nodeValue(heap, keeper.get()), static_cast<std::uint64_t>(999),
             "the rooted old object survives");
    expectTrue(heap.validate(), "the heap is well formed after a major collection");
}

void generationalLargeObjectTests() {
    gc::RootSet roots;
    gc::GenerationalHeap heap(4096, 32768, roots, 2);
    const gc::Ref large = heap.allocate(kTagBlob, 0, 2048);
    gc::Handle handle(roots, large);
    expectTrue(gc::isOldRef(large), "a large object is allocated straight into the old generation");
    expectEq(heap.stats().largeObjectAllocations, static_cast<std::size_t>(1),
             "large allocations are counted");
    gc::storeBytes(heap.payload(large), "large object payload");
    heap.minorCollect();
    expectEq(gc::loadBytes(heap.payload(handle.get()), 20), std::string("large object payload"),
             "a large object is never copied by a minor collection");
    expectEq(handle.get(), large, "a large object keeps its address");
}

void generationalPressureTests() {
    gc::RootSet roots;
    gc::GenerationalHeap heap(2048, 16384, roots, 3);
    gc::Handle retained(roots, makeNode(heap, 0, 2));
    gc::Handle chain(roots, gc::kNull);
    std::mt19937 rng(2024);

    std::size_t expectedLength = 0;
    bool allocated = true;
    for (int i = 0; i < 4000 && allocated; ++i) {
        const gc::Ref node = makeNode(heap, static_cast<std::uint64_t>(i));
        allocated = node != gc::kNull;
        if (!allocated) {
            break;
        }
        heap.setField(node, 0, chain.get());
        chain.set(node);
        ++expectedLength;
        if (rng() % 15 == 0) {
            heap.setField(retained.get(), rng() % 2, node);
        }
        if (rng() % 40 == 0) {
            chain.set(gc::kNull);
            expectedLength = 0;
        }
        if (rng() % 500 == 0) {
            heap.majorCollect();
        }
    }

    expectTrue(allocated, "allocation under generational pressure succeeds");
    expectTrue(heap.stats().minorCollections > 5, "the nursery fills repeatedly");
    expectTrue(heap.stats().objectsPromoted > 0, "long lived objects are promoted");
    expectEq(heap.stats().failedAllocations, static_cast<std::size_t>(0),
             "generational collection keeps allocation from failing");
    expectEq(nodeValue(heap, retained.get()), static_cast<std::uint64_t>(0),
             "the retained root keeps its payload");
    expectTrue(heap.validate(), "the heap is well formed after generational pressure");

    std::size_t length = 0;
    for (gc::Ref cursor = chain.get(); cursor != gc::kNull; cursor = heap.field(cursor, 0)) {
        ++length;
    }
    expectEq(length, expectedLength, "the surviving chain has the expected length");

    heap.majorCollect();
    expectTrue(heap.validate(), "a final major collection leaves the heap well formed");
    const std::size_t liveAfter = heap.liveObjectCount();
    chain.set(gc::kNull);
    heap.setField(retained.get(), 0, gc::kNull);
    heap.setField(retained.get(), 1, gc::kNull);
    heap.majorCollect();
    expectTrue(heap.liveObjectCount() <= liveAfter,
               "dropping references never grows the live set");
    expectEq(heap.liveObjectCount(), static_cast<std::size_t>(1),
             "only the retained root is left alive");
}

}

int main() {
    objectModelTests();
    rootSetTests();
    freeListTests();
    markSweepTests();
    markSweepPressureTests();
    markSweepRandomGraphTests();
    copyingTests();
    copyingPressureTests();
    generationalTests();
    generationalMajorTests();
    generationalLargeObjectTests();
    generationalPressureTests();

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
