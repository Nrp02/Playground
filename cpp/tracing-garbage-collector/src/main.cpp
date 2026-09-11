#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "copying.hpp"
#include "generational.hpp"
#include "mark_sweep.hpp"

namespace {

constexpr std::uint32_t kTagNode = 1;
constexpr std::uint32_t kTagPair = 2;
constexpr std::uint32_t kTagBlob = 3;

void banner(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

template <typename Heap>
gc::Ref makeNode(Heap& heap, std::uint64_t value) {
    const gc::Ref node = heap.allocate(kTagNode, 1, 8);
    if (node != gc::kNull) {
        gc::storeU64(heap.payload(node), 0, value);
    }
    return node;
}

template <typename Heap>
std::uint64_t nodeValue(const Heap& heap, gc::Ref node) {
    return gc::loadU64(heap.payload(node), 0);
}

void markSweepDemo() {
    banner("mark-sweep: cycles, fragmentation, reclamation");
    gc::RootSet roots;
    gc::MarkSweepHeap heap(16 * 1024, roots);

    gc::Handle head(roots, gc::kNull);
    for (std::uint64_t i = 0; i < 20; ++i) {
        const gc::Ref node = makeNode(heap, i);
        heap.setField(node, 0, head.get());
        head.set(node);
    }
    std::cout << "reachable list length: " << heap.reachable().size() << "\n";
    std::cout << "heap used=" << heap.usedBytes() << " free=" << heap.freeBytes()
              << " blocks=" << heap.liveObjects().size() << "\n";

    const gc::Ref a = makeNode(heap, 900);
    const gc::Ref b = makeNode(heap, 901);
    heap.setField(a, 0, b);
    heap.setField(b, 0, a);
    std::cout << "created an unreachable 2-node cycle\n";

    heap.collect();
    std::cout << "after collect: objects=" << heap.liveObjects().size()
              << " used=" << heap.usedBytes() << " reclaimed="
              << heap.stats().bytesReclaimed << " bytes, collected="
              << heap.stats().objectsCollected << " objects\n";

    gc::Ref cursor = head.get();
    std::uint64_t sum = 0;
    std::size_t length = 0;
    while (cursor != gc::kNull) {
        sum += nodeValue(heap, cursor);
        ++length;
        cursor = heap.field(cursor, 0);
    }
    std::cout << "survivor list length=" << length << " value sum=" << sum << "\n";

    std::vector<gc::Handle> pinned;
    for (int i = 0; i < 40; ++i) {
        pinned.emplace_back(roots, heap.allocate(kTagBlob, 0, 64));
    }
    for (std::size_t i = 0; i < pinned.size(); i += 2) {
        pinned[i].release();
    }
    heap.collect();
    std::cout << "after freeing every other blob: freeBlocks=" << heap.freeBlockCount()
              << " largestFreeBlock=" << heap.largestFreeBlock()
              << " totalFree=" << heap.freeBytes() << "\n";
    std::cout << "heap structure valid: " << std::boolalpha << heap.validate() << "\n";

    head.release();
    pinned.clear();
    heap.collect();
    std::cout << "after dropping every root: objects=" << heap.liveObjects().size()
              << " used=" << heap.usedBytes() << " collections=" << heap.stats().collections
              << "\n";
}

void copyingDemo() {
    banner("copying: Cheney evacuation and compaction");
    gc::RootSet roots;
    gc::CopyingHeap heap(16 * 1024, roots);

    gc::Handle shared(roots, makeNode(heap, 42));
    gc::Handle left(roots, heap.allocate(kTagPair, 2, 8));
    gc::Handle right(roots, heap.allocate(kTagPair, 2, 8));
    heap.setField(left.get(), 0, shared.get());
    heap.setField(right.get(), 0, shared.get());

    for (std::uint64_t i = 0; i < 60; ++i) {
        makeNode(heap, i);
    }
    std::cout << "before collect: used=" << heap.usedBytes()
              << " objects=" << heap.liveObjects().size() << "\n";

    const gc::Ref sharedBefore = shared.get();
    heap.collect();
    std::cout << "after collect: used=" << heap.usedBytes()
              << " objects=" << heap.liveObjects().size()
              << " copied=" << heap.stats().objectsCopied << "\n";
    std::cout << "shared node moved " << sharedBefore << " -> " << shared.get()
              << " value=" << nodeValue(heap, shared.get()) << "\n";
    std::cout << "both parents still point at one shared copy: " << std::boolalpha
              << (heap.field(left.get(), 0) == heap.field(right.get(), 0)) << "\n";

    gc::Handle chain(roots, gc::kNull);
    std::mt19937 rng(7);
    for (int round = 0; round < 2000; ++round) {
        const gc::Ref node = makeNode(heap, rng());
        heap.setField(node, 0, chain.get());
        chain.set(node);
        if (round % 25 == 0) {
            chain.set(gc::kNull);
        }
    }
    std::cout << "after 2000 allocations with churn: collections=" << heap.stats().collections
              << " bytesCopied=" << heap.stats().bytesCopied
              << " bytesReclaimed=" << heap.stats().bytesReclaimed << "\n";
    std::cout << "to-space is contiguous (used == sum of live sizes): " << std::boolalpha
              << heap.validate() << "\n";
}

void generationalDemo() {
    banner("generational: nursery churn, promotion, write barrier");
    gc::RootSet roots;
    gc::GenerationalHeap heap(8 * 1024, 64 * 1024, roots, 2);

    gc::Handle survivor(roots, makeNode(heap, 1));
    std::cout << "promotion age threshold: " << static_cast<int>(heap.promotionAge()) << "\n";

    for (int round = 0; round < 12; ++round) {
        for (int i = 0; i < 40; ++i) {
            makeNode(heap, static_cast<std::uint64_t>(i));
        }
        heap.minorCollect();
    }
    std::cout << "minorCollections=" << heap.stats().minorCollections
              << " copied=" << heap.stats().objectsCopied
              << " promoted=" << heap.stats().objectsPromoted << "\n";
    std::cout << "survivor now lives in the old generation: " << std::boolalpha
              << gc::isOldRef(survivor.get()) << " value=" << nodeValue(heap, survivor.get())
              << "\n";

    const gc::Ref fresh = makeNode(heap, 777);
    heap.setField(survivor.get(), 0, fresh);
    std::cout << "old->young write recorded, rememberedSetSize=" << heap.rememberedSetSize()
              << "\n";

    heap.minorCollect();
    const gc::Ref moved = heap.field(survivor.get(), 0);
    std::cout << "young object reachable only from the old generation survived: " << std::boolalpha
              << (moved != gc::kNull && nodeValue(heap, moved) == 777) << "\n";
    std::cout << "remembered set after the minor collection: " << heap.rememberedSetSize() << "\n";

    std::cout << "young used=" << heap.youngUsedBytes() << "/" << heap.youngCapacity()
              << " old used=" << heap.oldUsedBytes() << "/" << heap.oldCapacity() << "\n";

    heap.setField(survivor.get(), 0, gc::kNull);

    gc::Handle doomed(roots, gc::kNull);
    for (std::uint64_t i = 0; i < 30; ++i) {
        const gc::Ref node = makeNode(heap, 1000 + i);
        heap.setField(node, 0, doomed.get());
        doomed.set(node);
    }
    for (int i = 0; i < 3; ++i) {
        heap.minorCollect();
    }
    std::cout << "doomed chain promoted to the old generation: " << std::boolalpha
              << gc::isOldRef(doomed.get()) << " oldUsed=" << heap.oldUsedBytes() << "\n";
    doomed.release();

    heap.majorCollect();
    std::cout << "after major collect: majorCollections=" << heap.stats().majorCollections
              << " oldObjectsCollected=" << heap.stats().oldObjectsCollected
              << " oldBytesReclaimed=" << heap.stats().oldBytesReclaimed << "\n";
    std::cout << "live objects=" << heap.liveObjectCount()
              << " (young=" << heap.youngObjects().size() << ")\n";
    std::cout << "heap structure valid: " << std::boolalpha << heap.validate() << "\n";

    std::cout << "barrier writes=" << heap.stats().barrierWrites
              << " remembered additions=" << heap.stats().rememberedAdditions
              << " large objects=" << heap.stats().largeObjectAllocations << "\n";
}

void workloadComparison() {
    banner("side by side on the same workload");
    const int rounds = 3000;

    gc::RootSet msRoots;
    gc::MarkSweepHeap markSweep(64 * 1024, msRoots);
    gc::Handle msChain(msRoots, gc::kNull);

    gc::RootSet copyRoots;
    gc::CopyingHeap copying(64 * 1024, copyRoots);
    gc::Handle copyChain(copyRoots, gc::kNull);

    gc::RootSet genRoots;
    gc::GenerationalHeap generational(16 * 1024, 64 * 1024, genRoots, 3);
    gc::Handle genChain(genRoots, gc::kNull);

    for (int i = 0; i < rounds; ++i) {
        const gc::Ref m = makeNode(markSweep, static_cast<std::uint64_t>(i));
        markSweep.setField(m, 0, msChain.get());
        msChain.set(m);

        const gc::Ref c = makeNode(copying, static_cast<std::uint64_t>(i));
        copying.setField(c, 0, copyChain.get());
        copyChain.set(c);

        const gc::Ref g = makeNode(generational, static_cast<std::uint64_t>(i));
        generational.setField(g, 0, genChain.get());
        genChain.set(g);

        if (i % 30 == 29) {
            msChain.set(gc::kNull);
            copyChain.set(gc::kNull);
            genChain.set(gc::kNull);
        }
    }

    std::cout << std::left << std::setw(16) << "collector" << std::setw(14) << "collections"
              << std::setw(14) << "allocated" << std::setw(14) << "reclaimed" << "moved\n";
    std::cout << std::setw(16) << "mark-sweep" << std::setw(14) << markSweep.stats().collections
              << std::setw(14) << markSweep.stats().bytesAllocated << std::setw(14)
              << markSweep.stats().bytesReclaimed << 0 << "\n";
    std::cout << std::setw(16) << "copying" << std::setw(14) << copying.stats().collections
              << std::setw(14) << copying.stats().bytesAllocated << std::setw(14)
              << copying.stats().bytesReclaimed << copying.stats().bytesCopied << "\n";
    std::cout << std::setw(16) << "generational"
              << std::setw(14)
              << generational.stats().minorCollections + generational.stats().majorCollections
              << std::setw(14) << generational.stats().bytesAllocated << std::setw(14)
              << generational.stats().youngBytesReclaimed + generational.stats().oldBytesReclaimed
              << generational.stats().bytesCopied + generational.stats().bytesPromoted << "\n";
    std::cout << std::right;
}

}

int main() {
    markSweepDemo();
    copyingDemo();
    generationalDemo();
    workloadComparison();
    std::cout << "\ndone\n";
    return 0;
}
