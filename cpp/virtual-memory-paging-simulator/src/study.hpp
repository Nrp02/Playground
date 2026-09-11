#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "paging_simulator.hpp"

namespace paging {

struct StudyResult {
    std::uint64_t references = 0;
    std::uint64_t pageFaults = 0;
    std::uint64_t majorFaults = 0;
    std::uint64_t minorFaults = 0;
    std::uint64_t evictions = 0;
    std::uint64_t swapOuts = 0;
    double faultRate = 0.0;
    double tlbHitRate = 0.0;
    double effectiveAccessNanos = 0.0;
};

inline StudyResult runReferenceString(const std::vector<std::uint64_t>& refs, std::size_t frames,
                                      const std::string& policyName, bool writeEvery = false,
                                      std::size_t pageSizeBytes = 256) {
    Config config;
    config.pageSizeBytes = pageSizeBytes;
    config.frameCount = frames;
    config.tlbSets = 4;
    config.tlbWays = 2;
    config.workingSetWindow = 32;
    Simulator sim(config, policyName, Allocation::Global);
    int pid = sim.createProcess();
    std::uint64_t maxPage = 0;
    for (std::uint64_t vpn : refs) {
        maxPage = std::max(maxPage, vpn);
    }
    sim.mapPages(pid, 0, static_cast<std::size_t>(maxPage) + 1);
    std::vector<std::uint64_t> future;
    future.reserve(refs.size());
    for (std::uint64_t vpn : refs) {
        future.push_back(makeKey(pid, vpn));
    }
    sim.provideFuture(future);
    for (std::size_t i = 0; i < refs.size(); ++i) {
        sim.touch(pid, refs[i], writeEvery && (i % 3 == 0));
    }
    Stats stats = sim.totalStats();
    StudyResult result;
    result.references = stats.references;
    result.pageFaults = stats.pageFaults;
    result.majorFaults = stats.majorFaults;
    result.minorFaults = stats.minorFaults;
    result.evictions = stats.evictions;
    result.swapOuts = stats.swapOuts;
    result.faultRate = stats.references == 0
                           ? 0.0
                           : static_cast<double>(stats.pageFaults) / static_cast<double>(stats.references);
    result.tlbHitRate = sim.tlb().hitRate();
    result.effectiveAccessNanos = sim.effectiveAccessTimeNanos(Latency());
    return result;
}

}
