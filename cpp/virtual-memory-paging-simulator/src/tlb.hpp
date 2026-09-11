#pragma once

#include <cstdint>
#include <vector>

#include "paging_types.hpp"

namespace paging {

class Tlb {
public:
    Tlb(std::size_t sets, std::size_t ways, bool tagged)
        : sets_(sets == 0 ? 1 : sets), ways_(ways == 0 ? 1 : ways), tagged_(tagged),
          entries_(sets_ * (ways == 0 ? 1 : ways)) {}

    bool lookup(int asid, std::uint64_t vpn, std::size_t& frameOut, bool& writableOut) {
        Entry* entry = locate(asid, vpn);
        if (entry == nullptr) {
            ++misses_;
            return false;
        }
        ++hits_;
        entry->lastUse = ++clock_;
        frameOut = entry->frame;
        writableOut = entry->writable;
        return true;
    }

    void insert(int asid, std::uint64_t vpn, std::size_t frame, bool writable) {
        std::size_t base = (vpn % sets_) * ways_;
        std::size_t victim = base;
        for (std::size_t i = 0; i < ways_; ++i) {
            Entry& candidate = entries_[base + i];
            if (!candidate.valid) {
                victim = base + i;
                break;
            }
            if (candidate.lastUse < entries_[victim].lastUse) {
                victim = base + i;
            }
        }
        if (entries_[victim].valid) {
            ++replacements_;
        }
        entries_[victim] = Entry{true, asid, vpn, frame, writable, ++clock_};
    }

    void invalidate(int asid, std::uint64_t vpn) {
        Entry* entry = locate(asid, vpn);
        if (entry != nullptr) {
            entry->valid = false;
        }
    }

    void flush() {
        for (Entry& entry : entries_) {
            entry.valid = false;
        }
        ++flushes_;
    }

    void onContextSwitch() {
        if (!tagged_) {
            flush();
        }
    }

    std::size_t validEntries() const {
        std::size_t count = 0;
        for (const Entry& entry : entries_) {
            if (entry.valid) {
                ++count;
            }
        }
        return count;
    }

    std::size_t capacity() const { return entries_.size(); }
    std::size_t reachBytes(std::size_t pageSizeBytes) const { return entries_.size() * pageSizeBytes; }
    std::uint64_t hits() const { return hits_; }
    std::uint64_t misses() const { return misses_; }
    std::uint64_t flushes() const { return flushes_; }
    std::uint64_t replacements() const { return replacements_; }

    double hitRate() const {
        std::uint64_t total = hits_ + misses_;
        return total == 0 ? 0.0 : static_cast<double>(hits_) / static_cast<double>(total);
    }

    void resetStats() {
        hits_ = 0;
        misses_ = 0;
        flushes_ = 0;
        replacements_ = 0;
    }

private:
    struct Entry {
        bool valid = false;
        int asid = 0;
        std::uint64_t vpn = 0;
        std::size_t frame = 0;
        bool writable = false;
        std::uint64_t lastUse = 0;
    };

    Entry* locate(int asid, std::uint64_t vpn) {
        std::size_t base = (vpn % sets_) * ways_;
        for (std::size_t i = 0; i < ways_; ++i) {
            Entry& entry = entries_[base + i];
            if (entry.valid && entry.vpn == vpn && entry.asid == asid) {
                return &entry;
            }
        }
        return nullptr;
    }

    std::size_t sets_;
    std::size_t ways_;
    bool tagged_;
    std::vector<Entry> entries_;
    std::uint64_t clock_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t flushes_ = 0;
    std::uint64_t replacements_ = 0;
};

}
