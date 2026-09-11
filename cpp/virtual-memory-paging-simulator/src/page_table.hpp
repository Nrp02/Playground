#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "paging_types.hpp"

namespace paging {

class PageTable {
public:
    PageTable(unsigned virtualAddressBits, unsigned offsetBits) {
        if (virtualAddressBits <= offsetBits) {
            throw PagingError("virtual address bits must exceed page offset bits");
        }
        unsigned indexBits = virtualAddressBits - offsetBits;
        secondLevelBits_ = indexBits / 2;
        firstLevelBits_ = indexBits - secondLevelBits_;
        secondLevelSize_ = static_cast<std::size_t>(1) << secondLevelBits_;
        maxVpn_ = (static_cast<std::uint64_t>(1) << indexBits) - 1;
    }

    PageTableEntry* find(std::uint64_t vpn) {
        if (vpn > maxVpn_) {
            return nullptr;
        }
        auto it = directory_.find(vpn >> secondLevelBits_);
        if (it == directory_.end()) {
            return nullptr;
        }
        return &it->second[vpn & (secondLevelSize_ - 1)];
    }

    const PageTableEntry* find(std::uint64_t vpn) const {
        return const_cast<PageTable*>(this)->find(vpn);
    }

    PageTableEntry& mapPage(std::uint64_t vpn, bool writable) {
        if (vpn > maxVpn_) {
            throw SegmentationFault("virtual page number outside address space");
        }
        auto it = directory_.find(vpn >> secondLevelBits_);
        if (it == directory_.end()) {
            it = directory_.emplace(vpn >> secondLevelBits_,
                                    std::vector<PageTableEntry>(secondLevelSize_)).first;
        }
        PageTableEntry& entry = it->second[vpn & (secondLevelSize_ - 1)];
        entry.mapped = true;
        entry.writable = writable;
        return entry;
    }

    std::vector<std::uint64_t> mappedPages() const {
        std::vector<std::uint64_t> pages;
        for (const auto& table : directory_) {
            for (std::size_t i = 0; i < table.second.size(); ++i) {
                if (table.second[i].mapped) {
                    pages.push_back((table.first << secondLevelBits_) | i);
                }
            }
        }
        std::sort(pages.begin(), pages.end());
        return pages;
    }

    std::size_t secondLevelTables() const { return directory_.size(); }
    std::size_t entriesPerTable() const { return secondLevelSize_; }
    unsigned firstLevelBits() const { return firstLevelBits_; }
    unsigned secondLevelBits() const { return secondLevelBits_; }
    std::uint64_t maxVpn() const { return maxVpn_; }

    std::size_t tableBytes(std::size_t entryBytes) const {
        std::size_t topLevel = (static_cast<std::size_t>(1) << firstLevelBits_) * sizeof(void*);
        return topLevel + directory_.size() * secondLevelSize_ * entryBytes;
    }

    std::size_t flatTableBytes(std::size_t entryBytes) const {
        return static_cast<std::size_t>(maxVpn_ + 1) * entryBytes;
    }

private:
    unsigned firstLevelBits_ = 0;
    unsigned secondLevelBits_ = 0;
    std::size_t secondLevelSize_ = 0;
    std::uint64_t maxVpn_ = 0;
    std::unordered_map<std::uint64_t, std::vector<PageTableEntry>> directory_;
};

}
