#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace paging {

class Workload {
public:
    static std::vector<std::uint64_t> sequentialScan(std::uint64_t pages, std::size_t passes) {
        std::vector<std::uint64_t> refs;
        refs.reserve(static_cast<std::size_t>(pages) * passes);
        for (std::size_t pass = 0; pass < passes; ++pass) {
            for (std::uint64_t page = 0; page < pages; ++page) {
                refs.push_back(page);
            }
        }
        return refs;
    }

    static std::vector<std::uint64_t> loopingReference(std::uint64_t loopPages, std::size_t loops) {
        return sequentialScan(loopPages, loops);
    }

    static std::vector<std::uint64_t> uniformRandom(std::uint64_t pages, std::size_t count,
                                                    std::uint32_t seed) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<std::uint64_t> dist(0, pages - 1);
        std::vector<std::uint64_t> refs;
        refs.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            refs.push_back(dist(rng));
        }
        return refs;
    }

    static std::vector<std::uint64_t> localityPhases(std::uint64_t totalPages,
                                                     std::size_t phaseCount,
                                                     std::size_t workingSetPages,
                                                     std::size_t phaseLength,
                                                     std::uint32_t seed) {
        std::mt19937 rng(seed);
        std::vector<std::uint64_t> refs;
        refs.reserve(phaseCount * phaseLength);
        std::uniform_int_distribution<std::uint64_t> base(0, totalPages - workingSetPages);
        std::uniform_int_distribution<std::size_t> within(0, workingSetPages - 1);
        for (std::size_t phase = 0; phase < phaseCount; ++phase) {
            std::uint64_t start = base(rng);
            for (std::size_t i = 0; i < phaseLength; ++i) {
                refs.push_back(start + within(rng));
            }
        }
        return refs;
    }

    static std::vector<std::uint64_t> skewed(std::uint64_t pages, std::size_t count,
                                             double hotFraction, double hotProbability,
                                             std::uint32_t seed) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> coin(0.0, 1.0);
        std::uint64_t hotPages = static_cast<std::uint64_t>(pages * hotFraction);
        if (hotPages == 0) {
            hotPages = 1;
        }
        std::uniform_int_distribution<std::uint64_t> hot(0, hotPages - 1);
        std::uniform_int_distribution<std::uint64_t> cold(hotPages, pages - 1);
        std::vector<std::uint64_t> refs;
        refs.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            refs.push_back(coin(rng) < hotProbability ? hot(rng) : cold(rng));
        }
        return refs;
    }
};

}
