#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backing_store.hpp"
#include "page_table.hpp"
#include "paging_types.hpp"
#include "replacement.hpp"
#include "tlb.hpp"
#include "working_set.hpp"

namespace paging {

enum class Allocation { Global, Local };

struct FrameInfo {
    bool occupied = false;
    bool pinned = false;
    int pid = -1;
    std::uint64_t vpn = 0;
};

struct ProcessReport {
    int pid = 0;
    std::size_t residentPages = 0;
    std::size_t mappedPages = 0;
    std::size_t workingSetSize = 0;
    std::size_t frameQuota = 0;
    double faultRate = 0.0;
    Stats stats;
};

class Simulator {
public:
    Simulator(const Config& config, const std::string& policyName, Allocation allocation)
        : config_(config), offsetBits_(shiftForPageSize(config.pageSizeBytes)),
          allocation_(allocation),
          policy_(makePolicy(policyName, config.frameCount, config.workingSetWindow)),
          tlb_(config.tlbSets, config.tlbWays, config.taggedTlb),
          store_(config.pageSizeBytes), frames_(config.frameCount),
          memory_(config.frameCount * config.pageSizeBytes, 0) {
        if (config.frameCount == 0) {
            throw PagingError("physical memory needs at least one frame");
        }
        for (std::size_t i = config.frameCount; i > 0; --i) {
            freeFrames_.push_back(i - 1);
        }
    }

    int createProcess(std::size_t frameQuota = 0) {
        int pid = nextPid_++;
        processes_.emplace(pid, ProcessState(pid, config_, offsetBits_, frameQuota));
        order_.push_back(pid);
        if (currentPid_ < 0) {
            currentPid_ = pid;
        }
        return pid;
    }

    void mapRange(int pid, std::uint64_t baseAddress, std::size_t lengthBytes, bool writable = true) {
        ProcessState& process = lookup(pid);
        std::uint64_t firstVpn = baseAddress >> offsetBits_;
        std::uint64_t lastVpn = (baseAddress + lengthBytes - 1) >> offsetBits_;
        for (std::uint64_t vpn = firstVpn; vpn <= lastVpn; ++vpn) {
            process.table.mapPage(vpn, writable);
        }
    }

    void mapPages(int pid, std::uint64_t firstVpn, std::size_t pageCount, bool writable = true) {
        mapRange(pid, firstVpn << offsetBits_, pageCount * config_.pageSizeBytes, writable);
    }

    void contextSwitch(int pid) {
        lookup(pid);
        if (pid != currentPid_) {
            ++contextSwitches_;
            tlb_.onContextSwitch();
            currentPid_ = pid;
        }
    }

    std::uint8_t read(int pid, std::uint64_t address) {
        std::size_t frame = translate(pid, address, false);
        return memory_[frame * config_.pageSizeBytes + (address & offsetMask())];
    }

    void write(int pid, std::uint64_t address, std::uint8_t value) {
        std::size_t frame = translate(pid, address, true);
        memory_[frame * config_.pageSizeBytes + (address & offsetMask())] = value;
    }

    bool touch(int pid, std::uint64_t vpn, bool isWrite = false) {
        std::size_t faultsBefore = totalStats().pageFaults;
        translate(pid, vpn << offsetBits_, isWrite);
        return totalStats().pageFaults != faultsBefore;
    }

    void pin(int pid, std::uint64_t vpn) {
        std::size_t frame = translate(pid, vpn << offsetBits_, false);
        frames_[frame].pinned = true;
    }

    void unpin(int pid, std::uint64_t vpn) {
        ProcessState& process = lookup(pid);
        PageTableEntry* entry = process.table.find(vpn);
        if (entry != nullptr && entry->present) {
            frames_[entry->frame].pinned = false;
        }
    }

    void provideFuture(const std::vector<std::uint64_t>& keys) { policy_->setFuture(keys); }

    void setFrameQuota(int pid, std::size_t quota) {
        ProcessState& process = lookup(pid);
        process.frameQuota = quota;
        while (allocation_ == Allocation::Local && quota > 0 && process.residentFrames.size() > quota) {
            std::vector<std::size_t> candidates = candidateFrames(process);
            if (candidates.empty()) {
                break;
            }
            evictFrame(policy_->evict(candidates));
        }
    }

    bool rebalanceByFaultFrequency(std::size_t frameDelta) {
        if (allocation_ != Allocation::Local) {
            return false;
        }
        std::vector<int> hungry;
        std::vector<int> idle;
        for (int pid : order_) {
            ProcessState& process = lookup(pid);
            if (process.pff.needsMoreFrames()) {
                hungry.push_back(pid);
            } else if (process.pff.canGiveUpFrames() && process.frameQuota > frameDelta + 1) {
                idle.push_back(pid);
            }
        }
        if (hungry.empty() || idle.empty()) {
            return false;
        }
        bool changed = false;
        std::size_t donorIndex = 0;
        for (int pid : hungry) {
            if (donorIndex >= idle.size()) {
                break;
            }
            ProcessState& donor = lookup(idle[donorIndex]);
            ProcessState& taker = lookup(pid);
            setFrameQuota(donor.pid, donor.frameQuota - frameDelta);
            taker.frameQuota += frameDelta;
            taker.pff.clear();
            donor.pff.clear();
            changed = true;
            ++donorIndex;
        }
        return changed;
    }

    std::size_t freeFrameCount() const { return freeFrames_.size(); }
    std::size_t frameCount() const { return config_.frameCount; }
    std::size_t pageSize() const { return config_.pageSizeBytes; }
    unsigned offsetBits() const { return offsetBits_; }
    std::uint64_t pageNumber(std::uint64_t address) const { return address >> offsetBits_; }
    std::uint64_t pageOffset(std::uint64_t address) const { return address & offsetMask(); }
    const Tlb& tlb() const { return tlb_; }
    Tlb& tlb() { return tlb_; }
    const BackingStore& backingStore() const { return store_; }
    const char* policyName() const { return policy_->name(); }
    std::uint64_t contextSwitches() const { return contextSwitches_; }
    const FrameInfo& frameInfo(std::size_t frame) const { return frames_.at(frame); }

    bool isResident(int pid, std::uint64_t vpn) {
        ProcessState& process = lookup(pid);
        const PageTableEntry* entry = process.table.find(vpn);
        return entry != nullptr && entry->present;
    }

    bool isDirty(int pid, std::uint64_t vpn) {
        ProcessState& process = lookup(pid);
        const PageTableEntry* entry = process.table.find(vpn);
        return entry != nullptr && entry->dirty;
    }

    std::size_t residentPages(int pid) { return lookup(pid).residentFrames.size(); }
    std::size_t workingSetSize(int pid) { return lookup(pid).workingSet.size(); }
    std::size_t frameQuota(int pid) { return lookup(pid).frameQuota; }
    double faultRate(int pid) { return lookup(pid).pff.faultRate(); }
    const PageTable& pageTable(int pid) { return lookup(pid).table; }
    const Stats& processStats(int pid) { return lookup(pid).stats; }

    Stats totalStats() const {
        Stats total;
        for (const auto& entry : processes_) {
            total.merge(entry.second.stats);
        }
        total.tlbHits = tlb_.hits();
        total.tlbMisses = tlb_.misses();
        return total;
    }

    std::vector<ProcessReport> reports() {
        std::vector<ProcessReport> result;
        for (int pid : order_) {
            ProcessState& process = lookup(pid);
            ProcessReport report;
            report.pid = pid;
            report.residentPages = process.residentFrames.size();
            report.mappedPages = process.table.mappedPages().size();
            report.workingSetSize = process.workingSet.size();
            report.frameQuota = process.frameQuota;
            report.faultRate = process.pff.faultRate();
            report.stats = process.stats;
            result.push_back(report);
        }
        return result;
    }

    double effectiveAccessTimeNanos(const Latency& latency) const {
        Stats stats = totalStats();
        if (stats.references == 0) {
            return 0.0;
        }
        double total = 0.0;
        total += static_cast<double>(stats.tlbHits) * (latency.tlbHitNanos + latency.memoryAccessNanos);
        total += static_cast<double>(stats.tlbMisses) *
                 (latency.tlbHitNanos + latency.pageTableWalkNanos + latency.memoryAccessNanos);
        total += static_cast<double>(stats.majorFaults) * latency.diskAccessNanos;
        total += static_cast<double>(stats.swapOuts) * latency.diskAccessNanos;
        return total / static_cast<double>(stats.references);
    }

private:
    struct ProcessState {
        ProcessState(int id, const Config& config, unsigned offsetBits, std::size_t quota)
            : pid(id), table(config.virtualAddressBits, offsetBits),
              workingSet(config.workingSetWindow),
              pff(config.pffWindow, config.pffUpperThreshold, config.pffLowerThreshold),
              frameQuota(quota == 0 ? config.frameCount : quota) {}

        int pid;
        PageTable table;
        WorkingSetTracker workingSet;
        PageFaultFrequencyMonitor pff;
        std::size_t frameQuota;
        std::unordered_set<std::size_t> residentFrames;
        Stats stats;
    };

    std::uint64_t offsetMask() const {
        return (static_cast<std::uint64_t>(1) << offsetBits_) - 1;
    }

    ProcessState& lookup(int pid) {
        auto it = processes_.find(pid);
        if (it == processes_.end()) {
            throw PagingError("unknown process id");
        }
        return it->second;
    }

    std::size_t translate(int pid, std::uint64_t address, bool isWrite) {
        ProcessState& process = lookup(pid);
        if (pid != currentPid_) {
            contextSwitch(pid);
        }
        std::uint64_t vpn = address >> offsetBits_;
        ++process.stats.references;
        if (isWrite) {
            ++process.stats.writes;
        } else {
            ++process.stats.reads;
        }
        process.workingSet.reference(vpn);
        policy_->tick(step_);

        std::size_t frame = 0;
        bool writable = false;
        bool faulted = false;
        if (tlb_.lookup(pid, vpn, frame, writable)) {
            if (isWrite && !writable) {
                throw ProtectionFault("write to read-only page");
            }
            PageTableEntry* entry = process.table.find(vpn);
            if (entry == nullptr || !entry->present) {
                throw PagingError("stale translation cached in TLB");
            }
            markAccessed(*entry, isWrite);
            policy_->onAccess(frame, makeKey(pid, vpn), step_);
        } else {
            PageTableEntry* entry = process.table.find(vpn);
            if (entry == nullptr || !entry->mapped) {
                throw SegmentationFault("access to unmapped virtual page");
            }
            if (isWrite && !entry->writable) {
                throw ProtectionFault("write to read-only page");
            }
            if (!entry->present) {
                handlePageFault(process, vpn, *entry);
                faulted = true;
            }
            frame = entry->frame;
            markAccessed(*entry, isWrite);
            policy_->onAccess(frame, makeKey(pid, vpn), step_);
            tlb_.insert(pid, vpn, frame, entry->writable);
        }
        process.pff.record(faulted);
        ++step_;
        return frame;
    }

    static void markAccessed(PageTableEntry& entry, bool isWrite) {
        entry.referenced = true;
        if (isWrite) {
            entry.dirty = true;
        }
    }

    void handlePageFault(ProcessState& process, std::uint64_t vpn, PageTableEntry& entry) {
        ++process.stats.pageFaults;
        std::size_t frame = acquireFrame(process);
        std::uint8_t* page = &memory_[frame * config_.pageSizeBytes];
        if (entry.swapSlot >= 0) {
            store_.readPage(entry.swapSlot, page);
            ++process.stats.majorFaults;
            ++process.stats.swapIns;
        } else {
            std::memset(page, 0, config_.pageSizeBytes);
            ++process.stats.minorFaults;
        }
        entry.present = true;
        entry.frame = frame;
        entry.dirty = false;
        entry.referenced = true;
        frames_[frame] = FrameInfo{true, false, process.pid, vpn};
        process.residentFrames.insert(frame);
        policy_->onFill(frame, makeKey(process.pid, vpn), step_);
    }

    std::size_t acquireFrame(ProcessState& process) {
        bool localLimited = allocation_ == Allocation::Local &&
                            process.residentFrames.size() >= process.frameQuota;
        if (!localLimited && !freeFrames_.empty()) {
            std::size_t frame = freeFrames_.back();
            freeFrames_.pop_back();
            return frame;
        }
        std::vector<std::size_t> candidates = candidateFrames(process);
        if (candidates.empty()) {
            if (!freeFrames_.empty()) {
                std::size_t frame = freeFrames_.back();
                freeFrames_.pop_back();
                return frame;
            }
            throw OutOfFrames("no evictable frame available");
        }
        std::size_t victim = policy_->evict(candidates);
        evictFrame(victim);
        auto it = std::find(freeFrames_.begin(), freeFrames_.end(), victim);
        if (it != freeFrames_.end()) {
            freeFrames_.erase(it);
        }
        return victim;
    }

    std::vector<std::size_t> candidateFrames(ProcessState& process) {
        std::vector<std::size_t> candidates;
        if (allocation_ == Allocation::Local ||
            process.residentFrames.size() >= process.frameQuota) {
            for (std::size_t frame : process.residentFrames) {
                if (!frames_[frame].pinned) {
                    candidates.push_back(frame);
                }
            }
        } else {
            for (std::size_t frame = 0; frame < frames_.size(); ++frame) {
                if (frames_[frame].occupied && !frames_[frame].pinned) {
                    candidates.push_back(frame);
                }
            }
        }
        std::sort(candidates.begin(), candidates.end());
        return candidates;
    }

    void evictFrame(std::size_t frame) {
        FrameInfo& info = frames_[frame];
        if (!info.occupied) {
            return;
        }
        ProcessState& owner = lookup(info.pid);
        PageTableEntry* entry = owner.table.find(info.vpn);
        if (entry != nullptr && entry->present) {
            if (entry->dirty) {
                if (entry->swapSlot < 0) {
                    entry->swapSlot = store_.allocateSlot();
                }
                store_.writePage(entry->swapSlot, &memory_[frame * config_.pageSizeBytes]);
                ++owner.stats.swapOuts;
            } else {
                ++owner.stats.cleanEvictions;
            }
            entry->present = false;
            entry->referenced = false;
            entry->dirty = false;
            entry->frame = 0;
        }
        tlb_.invalidate(info.pid, info.vpn);
        owner.residentFrames.erase(frame);
        ++owner.stats.evictions;
        policy_->onRemove(frame);
        info = FrameInfo();
        freeFrames_.push_back(frame);
    }

    Config config_;
    unsigned offsetBits_;
    Allocation allocation_;
    std::unique_ptr<ReplacementPolicy> policy_;
    Tlb tlb_;
    BackingStore store_;
    std::vector<FrameInfo> frames_;
    std::vector<std::uint8_t> memory_;
    std::vector<std::size_t> freeFrames_;
    std::unordered_map<int, ProcessState> processes_;
    std::vector<int> order_;
    int nextPid_ = 1;
    int currentPid_ = -1;
    std::uint64_t step_ = 0;
    std::uint64_t contextSwitches_ = 0;
};

}
