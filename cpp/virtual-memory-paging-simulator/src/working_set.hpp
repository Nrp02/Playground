#pragma once

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "paging_types.hpp"

namespace paging {

class WorkingSetTracker {
public:
    explicit WorkingSetTracker(std::size_t window) : window_(window == 0 ? 1 : window) {}

    void reference(std::uint64_t vpn) {
        recent_.push_back(vpn);
        ++counts_[vpn];
        while (recent_.size() > window_) {
            std::uint64_t old = recent_.front();
            recent_.pop_front();
            auto it = counts_.find(old);
            if (--it->second == 0) {
                counts_.erase(it);
            }
        }
    }

    bool contains(std::uint64_t vpn) const { return counts_.find(vpn) != counts_.end(); }
    std::size_t size() const { return counts_.size(); }
    std::size_t window() const { return window_; }
    std::size_t filled() const { return recent_.size(); }

    void setWindow(std::size_t window) {
        window_ = window == 0 ? 1 : window;
        while (recent_.size() > window_) {
            std::uint64_t old = recent_.front();
            recent_.pop_front();
            auto it = counts_.find(old);
            if (--it->second == 0) {
                counts_.erase(it);
            }
        }
    }

    std::vector<std::uint64_t> pages() const {
        std::vector<std::uint64_t> result;
        result.reserve(counts_.size());
        for (const auto& entry : counts_) {
            result.push_back(entry.first);
        }
        return result;
    }

    void clear() {
        recent_.clear();
        counts_.clear();
    }

private:
    std::size_t window_;
    std::deque<std::uint64_t> recent_;
    std::unordered_map<std::uint64_t, std::size_t> counts_;
};

class PageFaultFrequencyMonitor {
public:
    PageFaultFrequencyMonitor(std::size_t window, double upper, double lower)
        : window_(window == 0 ? 1 : window), upper_(upper), lower_(lower) {}

    void record(bool faulted) {
        history_.push_back(faulted);
        if (faulted) {
            ++faults_;
        }
        while (history_.size() > window_) {
            if (history_.front()) {
                --faults_;
            }
            history_.pop_front();
        }
    }

    double faultRate() const {
        if (history_.empty()) {
            return 0.0;
        }
        return static_cast<double>(faults_) / static_cast<double>(history_.size());
    }

    bool saturated() const { return history_.size() >= window_; }
    bool needsMoreFrames() const { return saturated() && faultRate() > upper_; }
    bool canGiveUpFrames() const { return saturated() && faultRate() < lower_; }

    void clear() {
        history_.clear();
        faults_ = 0;
    }

private:
    std::size_t window_;
    double upper_;
    double lower_;
    std::deque<bool> history_;
    std::size_t faults_ = 0;
};

}
