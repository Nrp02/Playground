#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "paging_simulator.hpp"
#include "study.hpp"
#include "workload.hpp"

using namespace paging;

namespace {

void banner(const std::string& title) {
    std::cout << "\n== " << title << " ==\n";
}

std::string percent(double value, int precision = 2) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value << "%";
    return out.str();
}

std::string nanos(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(0) << value << " ns";
    return out.str();
}

std::string bytes(std::size_t value) {
    std::ostringstream out;
    if (value >= 1024ULL * 1024ULL) {
        out << std::fixed << std::setprecision(2) << static_cast<double>(value) / (1024.0 * 1024.0) << " MiB";
    } else if (value >= 1024) {
        out << std::fixed << std::setprecision(2) << static_cast<double>(value) / 1024.0 << " KiB";
    } else {
        out << value << " B";
    }
    return out.str();
}

void demoTranslation() {
    banner("address translation and page-table shape");
    Config config;
    config.pageSizeBytes = 4096;
    config.frameCount = 32;
    config.virtualAddressBits = 32;
    Simulator sim(config, "LRU", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapRange(pid, 0x00400000, 64 * 1024);
    sim.mapRange(pid, 0x7FFF0000, 32 * 1024);

    std::uint64_t address = 0x00401234;
    std::cout << "page size " << config.pageSizeBytes << " B, offset bits " << sim.offsetBits()
              << ", virtual address bits " << config.virtualAddressBits << "\n";
    std::cout << "va 0x" << std::hex << address << std::dec << " -> vpn " << sim.pageNumber(address)
              << ", offset " << sim.pageOffset(address) << "\n";

    const PageTable& table = sim.pageTable(pid);
    std::cout << "two-level split: " << table.firstLevelBits() << " + " << table.secondLevelBits()
              << " + " << sim.offsetBits() << " bits\n";
    std::cout << "mapped pages " << table.mappedPages().size() << " across "
              << table.secondLevelTables() << " second-level tables\n";
    std::cout << "page-table memory " << bytes(table.tableBytes(sizeof(PageTableEntry)))
              << " sparse vs " << bytes(table.flatTableBytes(sizeof(PageTableEntry)))
              << " for a single flat table\n";
}

void demoDemandPaging() {
    banner("demand paging: zero fill, writeback, swap in");
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 4;
    Simulator sim(config, "LRU", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 16);

    std::cout << "fresh page reads as " << static_cast<int>(sim.read(pid, 0)) << " (zero fill on first touch)\n";
    sim.write(pid, 0, 42);
    sim.write(pid, 1 * config.pageSizeBytes, 7);
    for (std::uint64_t vpn = 2; vpn < 10; ++vpn) {
        sim.touch(pid, vpn);
    }
    std::cout << "page 0 resident after pressure: " << (sim.isResident(pid, 0) ? "yes" : "no") << "\n";
    std::cout << "value read back from page 0: " << static_cast<int>(sim.read(pid, 0))
              << " (restored from swap)\n";
    Stats stats = sim.totalStats();
    std::cout << "faults " << stats.pageFaults << " (minor " << stats.minorFaults << ", major "
              << stats.majorFaults << "), evictions " << stats.evictions << ", swap-outs "
              << stats.swapOuts << ", clean evictions " << stats.cleanEvictions << "\n";
    std::cout << "swap space in use " << bytes(sim.backingStore().bytesUsed()) << " for "
              << sim.backingStore().allocatedSlots() << " dirty pages only\n";
}

void demoTlb() {
    banner("TLB reach and context-switch cost");
    Config config;
    config.pageSizeBytes = 4096;
    config.frameCount = 256;
    config.tlbSets = 16;
    config.tlbWays = 4;
    Simulator sim(config, "LRU", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 256);
    std::cout << "TLB " << config.tlbSets << " sets x " << config.tlbWays << " ways = "
              << sim.tlb().capacity() << " entries, reach "
              << bytes(sim.tlb().reachBytes(config.pageSizeBytes)) << "\n";

    std::vector<std::uint64_t> inReach = Workload::sequentialScan(48, 40);
    for (std::uint64_t vpn : inReach) {
        sim.touch(pid, vpn);
    }
    std::cout << "looping over 48 pages (inside reach): TLB hit rate " << std::fixed
              << std::setprecision(2) << sim.tlb().hitRate() * 100.0 << "%\n";

    sim.tlb().resetStats();
    std::vector<std::uint64_t> outOfReach = Workload::sequentialScan(200, 10);
    for (std::uint64_t vpn : outOfReach) {
        sim.touch(pid, vpn);
    }
    std::cout << "looping over 200 pages (outside reach): TLB hit rate "
              << sim.tlb().hitRate() * 100.0 << "%\n";

    for (int tagged = 0; tagged < 2; ++tagged) {
        Config shared = config;
        shared.taggedTlb = tagged == 1;
        Simulator twin(shared, "LRU", Allocation::Global);
        int first = twin.createProcess();
        int second = twin.createProcess();
        twin.mapPages(first, 0, 8);
        twin.mapPages(second, 0, 8);
        for (int round = 0; round < 20; ++round) {
            for (int repeat = 0; repeat < 2; ++repeat) {
                for (std::uint64_t vpn = 0; vpn < 8; ++vpn) {
                    twin.touch(first, vpn);
                }
            }
            for (int repeat = 0; repeat < 2; ++repeat) {
                for (std::uint64_t vpn = 0; vpn < 8; ++vpn) {
                    twin.touch(second, vpn);
                }
            }
        }
        std::cout << "two processes time-slicing, " << (tagged == 1 ? "ASID-tagged" : "untagged  ")
                  << " TLB: " << twin.contextSwitches() << " context switches, "
                  << twin.tlb().flushes() << " flushes, hit rate "
                  << percent(twin.tlb().hitRate() * 100.0) << "\n";
    }
}

void demoPolicies() {
    banner("replacement policies under a phased locality workload");
    std::vector<std::uint64_t> refs = Workload::localityPhases(200, 12, 20, 400, 7);
    std::cout << std::left << std::setw(10) << "policy" << std::right << std::setw(10) << "faults"
              << std::setw(12) << "fault rate" << std::setw(12) << "swap-outs" << std::setw(14)
              << "eff. access" << "\n";
    for (const std::string& policy : {"FIFO", "CLOCK", "LRU", "WSCLOCK", "OPT"}) {
        StudyResult result = runReferenceString(refs, 24, policy, true);
        std::cout << std::left << std::setw(10) << policy << std::right << std::setw(10)
                  << result.pageFaults << std::setw(12) << percent(result.faultRate * 100.0)
                  << std::setw(12) << result.swapOuts << std::setw(14)
                  << nanos(result.effectiveAccessNanos) << "\n";
    }
    std::cout << "OPT is the offline lower bound; the gap to LRU is what a real policy leaves behind\n";
}

void demoBelady() {
    banner("Belady's anomaly: more frames, more faults");
    std::vector<std::uint64_t> refs = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    std::cout << "reference string 1 2 3 4 1 2 5 1 2 3 4 5\n";
    std::cout << std::left << std::setw(10) << "frames" << std::setw(10) << "FIFO" << std::setw(10)
              << "LRU" << std::setw(10) << "OPT" << "\n";
    for (std::size_t frames = 3; frames <= 4; ++frames) {
        std::cout << std::left << std::setw(10) << frames << std::setw(10)
                  << runReferenceString(refs, frames, "FIFO").pageFaults << std::setw(10)
                  << runReferenceString(refs, frames, "LRU").pageFaults << std::setw(10)
                  << runReferenceString(refs, frames, "OPT").pageFaults << "\n";
    }
    std::cout << "FIFO gets worse with a bigger memory; LRU and OPT are stack algorithms and cannot\n";
}

void demoWorkingSet() {
    banner("working-set size and the thrashing cliff");
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 64;
    config.workingSetWindow = 120;
    Simulator sim(config, "LRU", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 400);
    std::vector<std::uint64_t> refs = Workload::localityPhases(400, 4, 30, 600, 11);
    std::size_t sampled = 0;
    std::size_t sum = 0;
    for (std::size_t i = 0; i < refs.size(); ++i) {
        sim.touch(pid, refs[i]);
        if (i % 600 == 599) {
            std::cout << "after " << std::setw(4) << i + 1 << " refs: working set "
                      << sim.workingSetSize(pid) << " pages (window " << config.workingSetWindow
                      << "), resident " << sim.residentPages(pid) << "\n";
        }
        if (i >= config.workingSetWindow) {
            sum += sim.workingSetSize(pid);
            ++sampled;
        }
    }
    std::cout << "mean working-set size " << std::fixed << std::setprecision(1)
              << static_cast<double>(sum) / static_cast<double>(sampled) << " pages\n";

    std::vector<std::uint64_t> loop = Workload::localityPhases(400, 6, 30, 400, 3);
    std::cout << std::left << std::setw(10) << "frames" << std::setw(12) << "faults"
              << std::setw(14) << "fault rate" << std::setw(10) << "state" << "\n";
    for (std::size_t frames : {8, 16, 24, 30, 34, 48}) {
        StudyResult result = runReferenceString(loop, frames, "LRU");
        const char* state = result.faultRate > 0.20 ? "thrashing"
                            : result.faultRate > 0.09 ? "strained" : "comfortable";
        std::cout << std::left << std::setw(10) << frames << std::setw(12) << result.pageFaults
                  << std::setw(14) << percent(result.faultRate * 100.0) << std::setw(12) << state
                  << "\n";
    }
    std::cout << "the knee sits right where frames cross the 30-page working set\n";
}

void demoMultiprogramming() {
    banner("degree of multiprogramming: global vs local allocation");
    const std::size_t frames = 48;
    const std::size_t workingSet = 20;
    std::cout << std::left << std::setw(12) << "processes" << std::setw(11) << "frames ea"
              << std::setw(15) << "global faults" << std::setw(13) << "global rate"
              << std::setw(14) << "local faults" << std::setw(12) << "local rate" << "\n";
    for (std::size_t processCount : {1, 2, 3, 4, 6}) {
        std::uint64_t results[2] = {0, 0};
        double rate[2] = {0.0, 0.0};
        for (int mode = 0; mode < 2; ++mode) {
            Config config;
            config.pageSizeBytes = 256;
            config.frameCount = frames;
            config.workingSetWindow = 64;
            Simulator sim(config, "LRU", mode == 0 ? Allocation::Global : Allocation::Local);
            std::vector<int> pids;
            std::vector<std::vector<std::uint64_t>> streams;
            for (std::size_t i = 0; i < processCount; ++i) {
                int pid = sim.createProcess(mode == 0 ? 0 : frames / processCount);
                sim.mapPages(pid, 0, 120);
                pids.push_back(pid);
                streams.push_back(Workload::localityPhases(120, 4, workingSet, 500,
                                                           static_cast<std::uint32_t>(100 + i)));
            }
            for (std::size_t step = 0; step < streams[0].size(); ++step) {
                for (std::size_t i = 0; i < pids.size(); ++i) {
                    sim.touch(pids[i], streams[i][step], step % 4 == 0);
                }
            }
            Stats stats = sim.totalStats();
            results[mode] = stats.pageFaults;
            rate[mode] = static_cast<double>(stats.pageFaults) /
                         static_cast<double>(stats.references) * 100.0;
        }
        std::cout << std::left << std::setw(12) << processCount << std::setw(11)
                  << frames / processCount << std::setw(15) << results[0] << std::setw(13)
                  << percent(rate[0]) << std::setw(14) << results[1] << std::setw(12)
                  << percent(rate[1]) << "\n";
    }
    std::cout << "each process needs " << workingSet << " pages; once the per-process share of "
              << frames << " frames\n";
    std::cout << "drops below that, every added process buys paging traffic instead of work\n";
}

void demoPageFaultFrequency() {
    banner("page-fault-frequency control reallocating frames");
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

    std::size_t adjustments = 0;
    for (std::size_t i = 0; i < hungryStream.size(); ++i) {
        sim.touch(hungry, hungryStream[i]);
        sim.touch(modest, modestStream[i]);
        if (i % 40 == 39 && sim.rebalanceByFaultFrequency(2)) {
            ++adjustments;
        }
        if (i % 300 == 299) {
            std::cout << "after " << std::setw(4) << i + 1 << " refs each: quota "
                      << sim.frameQuota(hungry) << "/" << sim.frameQuota(modest)
                      << ", fault rate " << std::fixed << std::setprecision(3)
                      << sim.faultRate(hungry) << "/" << sim.faultRate(modest) << "\n";
        }
    }
    std::cout << adjustments << " reallocations moved frames from the process with slack to the one\n";
    std::cout << "faulting; final split " << sim.frameQuota(hungry) << " vs "
              << sim.frameQuota(modest) << " frames for working sets of 34 and 6 pages\n";
}

void demoPinning() {
    banner("pinned frames and frame exhaustion");
    Config config;
    config.pageSizeBytes = 256;
    config.frameCount = 4;
    Simulator sim(config, "FIFO", Allocation::Global);
    int pid = sim.createProcess();
    sim.mapPages(pid, 0, 32);
    sim.pin(pid, 0);
    sim.pin(pid, 1);
    for (std::uint64_t vpn = 2; vpn < 20; ++vpn) {
        sim.touch(pid, vpn);
    }
    std::cout << "pinned pages 0 and 1 still resident after 18 more pages: "
              << (sim.isResident(pid, 0) && sim.isResident(pid, 1) ? "yes" : "no") << "\n";
    sim.pin(pid, 19);
    sim.pin(pid, 18);
    try {
        sim.touch(pid, 5);
        std::cout << "unexpected: fault serviced with every frame pinned\n";
    } catch (const OutOfFrames& error) {
        std::cout << "with all 4 frames pinned the fault handler reports: " << error.what() << "\n";
    }
}

}

int main() {
    std::cout << "virtual memory paging simulator\n";
    demoTranslation();
    demoDemandPaging();
    demoTlb();
    demoPolicies();
    demoBelady();
    demoWorkingSet();
    demoMultiprogramming();
    demoPageFaultFrequency();
    demoPinning();
    std::cout << "\n";
    return 0;
}
