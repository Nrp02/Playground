#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace paging {

class PagingError : public std::runtime_error {
public:
    explicit PagingError(const std::string& what) : std::runtime_error(what) {}
};

class SegmentationFault : public PagingError {
public:
    explicit SegmentationFault(const std::string& what) : PagingError(what) {}
};

class ProtectionFault : public PagingError {
public:
    explicit ProtectionFault(const std::string& what) : PagingError(what) {}
};

class OutOfFrames : public PagingError {
public:
    explicit OutOfFrames(const std::string& what) : PagingError(what) {}
};

struct Config {
    std::size_t pageSizeBytes = 4096;
    std::size_t frameCount = 64;
    unsigned virtualAddressBits = 32;
    std::size_t tlbSets = 8;
    std::size_t tlbWays = 4;
    bool taggedTlb = false;
    std::size_t workingSetWindow = 64;
    std::size_t pffWindow = 64;
    double pffUpperThreshold = 0.30;
    double pffLowerThreshold = 0.05;
};

struct Latency {
    double tlbHitNanos = 1.0;
    double pageTableWalkNanos = 100.0;
    double memoryAccessNanos = 80.0;
    double diskAccessNanos = 3000000.0;
};

struct PageTableEntry {
    bool mapped = false;
    bool present = false;
    bool dirty = false;
    bool referenced = false;
    bool writable = true;
    std::size_t frame = 0;
    long swapSlot = -1;
};

struct Stats {
    std::uint64_t references = 0;
    std::uint64_t reads = 0;
    std::uint64_t writes = 0;
    std::uint64_t tlbHits = 0;
    std::uint64_t tlbMisses = 0;
    std::uint64_t pageFaults = 0;
    std::uint64_t minorFaults = 0;
    std::uint64_t majorFaults = 0;
    std::uint64_t evictions = 0;
    std::uint64_t swapIns = 0;
    std::uint64_t swapOuts = 0;
    std::uint64_t cleanEvictions = 0;

    void merge(const Stats& other) {
        references += other.references;
        reads += other.reads;
        writes += other.writes;
        tlbHits += other.tlbHits;
        tlbMisses += other.tlbMisses;
        pageFaults += other.pageFaults;
        minorFaults += other.minorFaults;
        majorFaults += other.majorFaults;
        evictions += other.evictions;
        swapIns += other.swapIns;
        swapOuts += other.swapOuts;
        cleanEvictions += other.cleanEvictions;
    }
};

inline std::uint64_t makeKey(int pid, std::uint64_t vpn) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pid)) << 40) | (vpn & 0xFFFFFFFFFFULL);
}

inline unsigned shiftForPageSize(std::size_t pageSizeBytes) {
    if (pageSizeBytes == 0 || (pageSizeBytes & (pageSizeBytes - 1)) != 0) {
        throw PagingError("page size must be a power of two");
    }
    unsigned bits = 0;
    while ((static_cast<std::size_t>(1) << bits) < pageSizeBytes) {
        ++bits;
    }
    return bits;
}

}
