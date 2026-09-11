#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../src/paging_simulator.hpp"
#include "../src/study.hpp"
#include "../src/workload.hpp"

using namespace paging;

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
        std::cerr << "FAIL: " << testName << " (got " << actual << ", expected " << expected << ")\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

std::size_t referenceModelFaults(const std::vector<std::uint64_t>& refs, std::size_t frames,
                                 const std::string& policy) {
    std::unordered_set<std::uint64_t> resident;
    std::deque<std::uint64_t> fifo;
    std::unordered_map<std::uint64_t, std::size_t> lastUse;
    std::size_t faults = 0;
    for (std::size_t i = 0; i < refs.size(); ++i) {
        std::uint64_t page = refs[i];
        if (resident.count(page) == 0) {
            ++faults;
            if (resident.size() == frames) {
                std::uint64_t victim = 0;
                if (policy == "FIFO") {
                    victim = fifo.front();
                    fifo.pop_front();
                } else if (policy == "LRU") {
                    std::size_t oldest = std::numeric_limits<std::size_t>::max();
                    for (std::uint64_t candidate : resident) {
                        if (lastUse[candidate] < oldest) {
                            oldest = lastUse[candidate];
                            victim = candidate;
                        }
                    }
                } else {
                    std::size_t farthest = 0;
                    bool first = true;
                    for (std::uint64_t candidate : resident) {
                        std::size_t next = std::numeric_limits<std::size_t>::max();
                        for (std::size_t j = i + 1; j < refs.size(); ++j) {
                            if (refs[j] == candidate) {
                                next = j;
                                break;
                            }
                        }
                        if (first || next > farthest) {
                            farthest = next;
                            victim = candidate;
                            first = false;
                        }
                    }
                }
                resident.erase(victim);
            }
            resident.insert(page);
            if (policy == "FIFO") {
                fifo.push_back(page);
            }
        }
        lastUse[page] = i;
    }
    return faults;
}

void testAddressTranslation() {
    Config config;
    config.pageSizeBytes = 4096;
    config.frameCount = 8;
    Simulator sim(config, "LRU", Allocation::Global);
    expectEq<unsigned>(sim.offsetBits(), 12u, "4KB pages give a 12-bit offset");
    expectEq<std::uint64_t>(sim.pageNumber(0x00401234), 1025u, "virtual page number extraction");
    expectEq<std::uint64_t>(sim.pageOffset(0x00401234), 0x234u, "page offset extraction");

    Config small = config;
    small.pageSizeBytes = 256;
    Simulator tiny(small, "LRU", Allocation::Global);
    expectEq<unsigned>(tiny.offsetBits(), 8u, "256B pages give an 8-bit offset");

    bool threw = false;
    try {
        Config bad = config;
        bad.pageSizeBytes = 300;
        Simulator invalid(bad, "LRU", Allocation::Global);
    } catch (const PagingError&) {
        threw = true;
    }
    expectTrue(threw, "non-power-of-two page size is rejected");
}

void testPageTableShape() {
    PageTable table(32, 12);
    expectEq<unsigned>(table.firstLevelBits() + table.secondLevelBits(), 20u,
                       "two-level index bits cover the virtual page number");
    expectEq<std::size_t>(table.secondLevelTables(), 0u, "no second-level tables before mapping");
    expectTrue(table.find(5) == nullptr, "lookup of an unmapped page returns null");

    table.mapPage(5, true);
    table.mapPage(6, true);
    expectEq<std::size_t>(table.secondLevelTables(), 1u, "nearby pages share one second-level table");
    table.mapPage(1u << 19, true);
    expectEq<std::size_t>(table.secondLevelTables(), 2u, "a distant page allocates a new table");
    expectEq<std::size_t>(table.mappedPages().size(), 3u, "mapped page enumeration");
    expectTrue(table.find(5) != nullptr && table.find(5)->mapped, "mapped entry is reachable");
    expectTrue(table.tableBytes(sizeof(PageTableEntry)) < table.flatTableBytes(sizeof(PageTableEntry)),
               "sparse two-level table beats a flat table");

    bool threw = false;
    try {
        table.mapPage(table.maxVpn() + 1, true);
    } catch (const SegmentationFault&) {
        threw = true;
    }
    expectTrue(threw, "mapping beyond the address space throws");
}

void testFaultsAndProtection() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 8;
    Simulator sim(config, "LRU", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 4);
    sim.mapPages(pid, 10, 2, false);

    bool segfault = false;
    try {
        sim.read(pid, 7 * 256);
    } catch (const SegmentationFault&) {
        segfault = true;
    }
    expectTrue(segfault, "access to an unmapped page segfaults");

    expectEq<int>(sim.read(pid, 10 * 256), 0, "read-only page faults in and reads as zero");
    bool protection = false;
    try {
        sim.write(pid, 10 * 256, 1);
    } catch (const ProtectionFault&) {
        protection = true;
    }
    expectTrue(protection, "write to a read-only page raises a protection fault");

    Stats stats = sim.totalStats();
    expectEq<std::uint64_t>(stats.pageFaults, stats.minorFaults + stats.majorFaults,
                            "faults split into minor and major");
    expectEq<std::uint64_t>(stats.references, stats.reads + stats.writes,
                            "references split into reads and writes");
}

void testDemandPagingAndSwap() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 3;
    Simulator sim(config, "LRU", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 12);

    expectEq<int>(sim.read(pid, 0), 0, "first touch is zero filled");
    expectEq<std::uint64_t>(sim.totalStats().minorFaults, 1u, "zero fill is a minor fault");
    expectEq<std::uint64_t>(sim.totalStats().majorFaults, 0u, "zero fill needs no disk read");
    expectEq<std::size_t>(sim.backingStore().allocatedSlots(), 0u,
                          "a clean page never occupies swap");

    for (std::uint64_t page = 0; page < 6; ++page) {
        sim.write(pid, page * 256 + 17, static_cast<std::uint8_t>(200 + page));
    }
    expectTrue(!sim.isResident(pid, 0), "the oldest page was evicted under pressure");
    expectTrue(sim.backingStore().allocatedSlots() > 0, "dirty pages were written to swap");

    for (std::uint64_t page = 0; page < 6; ++page) {
        expectEq<int>(sim.read(pid, page * 256 + 17), static_cast<int>(200 + page),
                      "page contents survive eviction and swap in");
    }
    Stats stats = sim.totalStats();
    expectTrue(stats.majorFaults > 0, "re-reading an evicted dirty page is a major fault");
    expectEq<std::uint64_t>(stats.swapIns, stats.majorFaults, "every major fault reads one page");

    Config cleanConfig = config;
    Simulator clean(cleanConfig, "LRU", Allocation::Global);
    int cleanPid = clean.createProcess();
    clean.mapPages(cleanPid, 0, 12);
    for (std::uint64_t page = 0; page < 10; ++page) {
        clean.touch(cleanPid, page);
    }
    expectEq<std::uint64_t>(clean.totalStats().swapOuts, 0u,
                            "clean eviction skips the writeback entirely");
    expectTrue(clean.totalStats().cleanEvictions > 0, "clean evictions are counted separately");
}

void testFrameAccounting() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 6;
    Simulator sim(config, "CLOCK", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 40);
    for (std::uint64_t page = 0; page < 40; ++page) {
        sim.touch(pid, page, page % 2 == 0);
    }
    expectEq<std::size_t>(sim.residentPages(pid), config.frameCount,
                          "resident set fills but never exceeds physical memory");
    std::size_t occupied = 0;
    for (std::size_t frame = 0; frame < sim.frameCount(); ++frame) {
        if (sim.frameInfo(frame).occupied) {
            ++occupied;
        }
    }
    expectEq<std::size_t>(occupied, config.frameCount, "every frame table slot is accounted for");
    expectEq<std::size_t>(sim.freeFrameCount(), 0u, "no frame is both free and occupied");
}

void testTlbBehaviour() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 16;
    config.tlbSets = 2;
    config.tlbWays = 2;
    Simulator sim(config, "LRU", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 16);

    sim.touch(pid, 0);
    expectEq<std::uint64_t>(sim.tlb().misses(), 1u, "the first translation misses the TLB");
    sim.touch(pid, 0);
    sim.touch(pid, 0);
    expectEq<std::uint64_t>(sim.tlb().hits(), 2u, "repeat translations hit the TLB");

    sim.touch(pid, 2);
    sim.touch(pid, 4);
    sim.touch(pid, 6);
    sim.touch(pid, 0);
    expectTrue(sim.tlb().replacements() > 0, "a 2-way set evicts on the third conflicting page");

    Config tagged = config;
    tagged.taggedTlb = true;
    Simulator taggedSim(tagged, "LRU", Allocation::Global);
    int first = taggedSim.createProcess();
    int second = taggedSim.createProcess();
    taggedSim.mapPages(first, 0, 4);
    taggedSim.mapPages(second, 0, 4);
    taggedSim.touch(first, 1);
    taggedSim.touch(second, 1);
    taggedSim.touch(first, 1);
    expectEq<std::uint64_t>(taggedSim.tlb().flushes(), 0u, "a tagged TLB survives context switches");
    expectEq<std::uint64_t>(taggedSim.tlb().hits(), 1u,
                            "ASIDs keep each process from reading the other's translation");

    Simulator untagged(config, "LRU", Allocation::Global);
    int a = untagged.createProcess();
    int b = untagged.createProcess();
    untagged.mapPages(a, 0, 4);
    untagged.mapPages(b, 0, 4);
    untagged.touch(a, 1);
    untagged.touch(b, 1);
    untagged.touch(a, 1);
    expectEq<std::uint64_t>(untagged.tlb().flushes(), untagged.contextSwitches(),
                            "an untagged TLB is flushed on every context switch");
    expectEq<std::uint64_t>(untagged.tlb().hits(), 0u, "a flushed TLB retains nothing");
}

void testTlbInvalidationOnEviction() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 2;
    config.tlbSets = 8;
    config.tlbWays = 4;
    Simulator sim(config, "FIFO", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 8);
    sim.write(pid, 0, 99);
    sim.touch(pid, 1);
    sim.touch(pid, 2);
    expectTrue(!sim.isResident(pid, 0), "page 0 was evicted");
    expectEq<int>(sim.read(pid, 0), 99,
                  "a stale TLB entry cannot resurrect an evicted frame");
}

void testBeladyAnomaly() {
    std::vector<std::uint64_t> refs = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    expectEq<std::uint64_t>(runReferenceString(refs, 3, "FIFO").pageFaults, 9u,
                            "FIFO takes 9 faults with 3 frames");
    expectEq<std::uint64_t>(runReferenceString(refs, 4, "FIFO").pageFaults, 10u,
                            "FIFO takes 10 faults with 4 frames: Belady's anomaly");
    expectTrue(runReferenceString(refs, 4, "LRU").pageFaults <=
                   runReferenceString(refs, 3, "LRU").pageFaults,
               "LRU is a stack algorithm and cannot show the anomaly");
    expectEq<std::uint64_t>(runReferenceString(refs, 3, "OPT").pageFaults, 7u,
                            "OPT takes 7 faults with 3 frames");
}

void testStackProperty() {
    std::vector<std::uint64_t> refs = Workload::uniformRandom(40, 3000, 5);
    std::uint64_t previous = std::numeric_limits<std::uint64_t>::max();
    bool monotone = true;
    for (std::size_t frames = 4; frames <= 40; frames += 4) {
        std::uint64_t faults = runReferenceString(refs, frames, "LRU").pageFaults;
        if (faults > previous) {
            monotone = false;
        }
        previous = faults;
    }
    expectTrue(monotone, "LRU faults never increase as memory grows");
}

void testOptimalIsALowerBound() {
    std::vector<std::uint64_t> refs = Workload::localityPhases(60, 8, 12, 200, 17);
    std::uint64_t optimal = runReferenceString(refs, 16, "OPT").pageFaults;
    bool bounded = true;
    for (const std::string& policy : {"FIFO", "LRU", "CLOCK", "WSCLOCK"}) {
        if (runReferenceString(refs, 16, policy).pageFaults < optimal) {
            bounded = false;
        }
    }
    expectTrue(bounded, "no online policy beats OPT");
}

void testAgainstReferenceModel() {
    std::mt19937 rng(4242);
    bool fifoMatches = true;
    bool lruMatches = true;
    bool optMatches = true;
    for (int trial = 0; trial < 40; ++trial) {
        std::uniform_int_distribution<std::uint64_t> pageDist(4, 20);
        std::uniform_int_distribution<std::size_t> frameDist(2, 8);
        std::uint64_t pages = pageDist(rng);
        std::size_t frames = frameDist(rng);
        std::vector<std::uint64_t> refs = Workload::uniformRandom(pages, 300, rng());
        if (runReferenceString(refs, frames, "FIFO").pageFaults != referenceModelFaults(refs, frames, "FIFO")) {
            fifoMatches = false;
        }
        if (runReferenceString(refs, frames, "LRU").pageFaults != referenceModelFaults(refs, frames, "LRU")) {
            lruMatches = false;
        }
        if (runReferenceString(refs, frames, "OPT").pageFaults != referenceModelFaults(refs, frames, "OPT")) {
            optMatches = false;
        }
    }
    expectTrue(fifoMatches, "FIFO matches an independent model over 40 randomized runs");
    expectTrue(lruMatches, "LRU matches an independent model over 40 randomized runs");
    expectTrue(optMatches, "OPT matches an independent model over 40 randomized runs");
}

void testClockGivesSecondChances() {
    ClockPolicy clock(3);
    clock.onFill(0, 100, 1);
    clock.onFill(1, 101, 2);
    clock.onFill(2, 102, 3);
    std::vector<std::size_t> candidates = {0, 1, 2};
    std::size_t victim = clock.evict(candidates);
    expectEq<std::size_t>(victim, 0u, "the hand clears every reference bit before evicting");
    expectEq<std::uint64_t>(clock.secondChances(), 3u, "each resident page got one second chance");

    clock.onRemove(0);
    clock.onFill(0, 103, 4);
    clock.onAccess(1, 101, 5);
    std::size_t next = clock.evict(candidates);
    expectEq<std::size_t>(next, 2u, "an unreferenced page is taken before a referenced one");
}

void testLoopingWorkloadDefeatsLru() {
    std::vector<std::uint64_t> refs = Workload::loopingReference(9, 30);
    StudyResult lru = runReferenceString(refs, 8, "LRU");
    expectEq<std::uint64_t>(lru.pageFaults, static_cast<std::uint64_t>(refs.size()),
                            "a loop one page larger than memory makes LRU fault on every access");
    StudyResult optimal = runReferenceString(refs, 8, "OPT");
    expectTrue(optimal.pageFaults < lru.pageFaults / 4,
               "OPT keeps most of the loop resident on the same memory");
}

void testWorkingSetTracker() {
    WorkingSetTracker tracker(4);
    tracker.reference(1);
    tracker.reference(2);
    tracker.reference(2);
    tracker.reference(3);
    expectEq<std::size_t>(tracker.size(), 3u, "working set counts distinct pages in the window");
    tracker.reference(4);
    expectTrue(!tracker.contains(1), "a page falls out once the window slides past it");
    expectEq<std::size_t>(tracker.size(), 3u, "window stays bounded");
    tracker.reference(5);
    tracker.reference(6);
    tracker.reference(7);
    tracker.reference(8);
    expectEq<std::size_t>(tracker.size(), 4u, "a fully distinct window has the window's size");
    tracker.setWindow(2);
    expectEq<std::size_t>(tracker.size(), 2u, "shrinking the window drops old references");
}

void testWorkingSetTracksLocality() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 64;
    config.workingSetWindow = 50;
    Simulator sim(config, "LRU", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 200);
    for (int i = 0; i < 200; ++i) {
        sim.touch(pid, static_cast<std::uint64_t>(i % 6));
    }
    expectEq<std::size_t>(sim.workingSetSize(pid), 6u, "a tight loop has a working set of its size");
    for (int i = 0; i < 200; ++i) {
        sim.touch(pid, static_cast<std::uint64_t>(100 + (i % 40)));
    }
    expectEq<std::size_t>(sim.workingSetSize(pid), 40u, "the working set follows a phase change");
}

void testFaultFrequencyMonitor() {
    PageFaultFrequencyMonitor monitor(10, 0.30, 0.10);
    expectTrue(!monitor.needsMoreFrames(), "an unfilled window does not trigger reallocation");
    for (int i = 0; i < 10; ++i) {
        monitor.record(i % 2 == 0);
    }
    expectTrue(monitor.saturated(), "the window fills after enough references");
    expectTrue(monitor.faultRate() > 0.49 && monitor.faultRate() < 0.51, "fault rate is 50%");
    expectTrue(monitor.needsMoreFrames(), "a 50% fault rate asks for more frames");
    for (int i = 0; i < 10; ++i) {
        monitor.record(false);
    }
    expectTrue(monitor.canGiveUpFrames(), "a quiet process can donate frames");
}

void testPinning() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 3;
    Simulator sim(config, "FIFO", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 20);
    sim.write(pid, 0, 55);
    sim.pin(pid, 0);
    for (std::uint64_t page = 1; page < 15; ++page) {
        sim.touch(pid, page);
    }
    expectTrue(sim.isResident(pid, 0), "a pinned page is never chosen as a victim");
    expectEq<int>(sim.read(pid, 0), 55, "the pinned frame still holds its data");

    sim.pin(pid, 13);
    sim.pin(pid, 14);
    bool exhausted = false;
    try {
        sim.touch(pid, 2);
    } catch (const OutOfFrames&) {
        exhausted = true;
    }
    expectTrue(exhausted, "a fault with every frame pinned reports frame exhaustion");

    sim.unpin(pid, 13);
    sim.touch(pid, 2);
    expectTrue(sim.isResident(pid, 2), "unpinning makes the frame available again");
}

void testLocalAllocationIsolatesProcesses() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 16;
    Simulator sim(config, "LRU", Allocation::Local);
    int noisy = sim.createProcess(8);
    int quiet = sim.createProcess(8);
    sim.mapPages(noisy, 0, 200);
    sim.mapPages(quiet, 0, 8);
    for (std::uint64_t page = 0; page < 4; ++page) {
        sim.touch(quiet, page);
    }
    for (std::uint64_t page = 0; page < 200; ++page) {
        sim.touch(noisy, page);
    }
    expectEq<std::size_t>(sim.residentPages(noisy), 8u, "a local quota caps the noisy process");
    expectEq<std::size_t>(sim.residentPages(quiet), 4u,
                          "the quiet process keeps every page it touched");
    expectEq<std::uint64_t>(sim.processStats(quiet).evictions, 0u,
                            "local replacement never steals another process's frames");
}

void testGlobalAllocationSharesFrames() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 16;
    Simulator sim(config, "LRU", Allocation::Global);
    int noisy = sim.createProcess();
    int quiet = sim.createProcess();
    sim.mapPages(noisy, 0, 200);
    sim.mapPages(quiet, 0, 8);
    for (std::uint64_t page = 0; page < 4; ++page) {
        sim.touch(quiet, page);
    }
    for (std::uint64_t page = 0; page < 200; ++page) {
        sim.touch(noisy, page);
    }
    expectTrue(sim.processStats(quiet).evictions > 0,
               "global replacement lets one process take another's frames");
    expectTrue(sim.residentPages(noisy) > 8u, "the noisy process grows past an even share");
}

void testFaultFrequencyRebalancing() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 40;
    config.pffWindow = 40;
    config.pffUpperThreshold = 0.12;
    config.pffLowerThreshold = 0.02;
    Simulator sim(config, "LRU", Allocation::Local);
    int hungry = sim.createProcess(20);
    int modest = sim.createProcess(20);
    sim.mapPages(hungry, 0, 120);
    sim.mapPages(modest, 0, 120);
    std::vector<std::uint64_t> hungryStream = Workload::localityPhases(120, 4, 34, 300, 21);
    std::vector<std::uint64_t> modestStream = Workload::localityPhases(120, 4, 6, 300, 22);
    for (std::size_t i = 0; i < hungryStream.size(); ++i) {
        sim.touch(hungry, hungryStream[i]);
        sim.touch(modest, modestStream[i]);
        if (i % 40 == 39) {
            sim.rebalanceByFaultFrequency(2);
        }
    }
    expectTrue(sim.frameQuota(hungry) > 20u, "the faulting process gained frames");
    expectTrue(sim.frameQuota(modest) < 20u, "the process with slack gave frames up");
    expectEq<std::size_t>(sim.frameQuota(hungry) + sim.frameQuota(modest), config.frameCount,
                          "reallocation conserves physical memory");
    expectTrue(sim.faultRate(hungry) < 0.12, "the fault rate fell back under the upper threshold");
}

void testThrashingCliff() {
    std::vector<std::uint64_t> refs = Workload::localityPhases(200, 6, 25, 400, 9);
    StudyResult starved = runReferenceString(refs, 8, "LRU", true);
    StudyResult fitted = runReferenceString(refs, 30, "LRU", true);
    expectTrue(starved.faultRate > 0.30, "memory well under the working set thrashes");
    expectTrue(fitted.faultRate < 0.05, "memory covering the working set does not");
    expectTrue(starved.effectiveAccessNanos > fitted.effectiveAccessNanos * 5.0,
               "thrashing dominates the effective access time");
    StudyResult readOnly = runReferenceString(refs, 8, "LRU");
    expectEq<std::uint64_t>(readOnly.majorFaults, 0u,
                            "a page never written is re-zero-filled instead of read from disk");
}

void testEffectiveAccessTime() {
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 32;
    Simulator sim(config, "LRU", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 8);
    for (int round = 0; round < 50; ++round) {
        for (std::uint64_t page = 0; page < 8; ++page) {
            sim.touch(pid, page);
        }
    }
    Latency latency;
    double eat = sim.effectiveAccessTimeNanos(latency);
    expectTrue(eat > latency.memoryAccessNanos && eat < latency.memoryAccessNanos + 20.0,
               "a fully resident working set costs little more than a memory access");
    expectTrue(sim.tlb().hitRate() > 0.95, "a small loop stays in the TLB");
}

void testStatsConsistency() {
    std::vector<std::uint64_t> refs = Workload::skewed(80, 2000, 0.1, 0.85, 31);
    StudyResult result = runReferenceString(refs, 20, "CLOCK", true);
    expectEq<std::uint64_t>(result.references, static_cast<std::uint64_t>(refs.size()),
                            "every reference is accounted for");
    expectEq<std::uint64_t>(result.pageFaults, result.minorFaults + result.majorFaults,
                            "fault classes sum to the total");
    expectTrue(result.evictions <= result.pageFaults, "there is never an eviction without a fault");
    expectTrue(result.swapOuts <= result.evictions, "only dirty evictions reach the disk");
    expectTrue(result.tlbHitRate > 0.0 && result.tlbHitRate < 1.0, "TLB hit rate is a proportion");
}

}

int main() {
    testAddressTranslation();
    testPageTableShape();
    testFaultsAndProtection();
    testDemandPagingAndSwap();
    testFrameAccounting();
    testTlbBehaviour();
    testTlbInvalidationOnEviction();
    testBeladyAnomaly();
    testStackProperty();
    testOptimalIsALowerBound();
    testAgainstReferenceModel();
    testClockGivesSecondChances();
    testLoopingWorkloadDefeatsLru();
    testWorkingSetTracker();
    testWorkingSetTracksLocality();
    testFaultFrequencyMonitor();
    testPinning();
    testLocalAllocationIsolatesProcesses();
    testGlobalAllocationSharesFrames();
    testFaultFrequencyRebalancing();
    testThrashingCliff();
    testEffectiveAccessTime();
    testStatsConsistency();

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
