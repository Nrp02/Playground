#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "paging_types.hpp"

namespace paging {

class BackingStore {
public:
    explicit BackingStore(std::size_t pageSizeBytes) : pageSizeBytes_(pageSizeBytes) {}

    long allocateSlot() {
        long slot = nextSlot_++;
        slots_.emplace(slot, std::vector<std::uint8_t>(pageSizeBytes_, 0));
        return slot;
    }

    void releaseSlot(long slot) {
        slots_.erase(slot);
    }

    void writePage(long slot, const std::uint8_t* data) {
        auto it = slots_.find(slot);
        if (it == slots_.end()) {
            throw PagingError("write to unallocated swap slot");
        }
        std::copy(data, data + pageSizeBytes_, it->second.begin());
        ++writes_;
    }

    void readPage(long slot, std::uint8_t* out) const {
        auto it = slots_.find(slot);
        if (it == slots_.end()) {
            throw PagingError("read from unallocated swap slot");
        }
        std::copy(it->second.begin(), it->second.end(), out);
        ++reads_;
    }

    std::size_t allocatedSlots() const { return slots_.size(); }
    std::uint64_t reads() const { return reads_; }
    std::uint64_t writes() const { return writes_; }
    std::size_t bytesUsed() const { return slots_.size() * pageSizeBytes_; }

private:
    std::size_t pageSizeBytes_;
    long nextSlot_ = 0;
    std::unordered_map<long, std::vector<std::uint8_t>> slots_;
    mutable std::uint64_t reads_ = 0;
    std::uint64_t writes_ = 0;
};

}
