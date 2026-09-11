#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "paging_types.hpp"

namespace paging {

class ReplacementPolicy {
public:
    virtual ~ReplacementPolicy() = default;
    virtual const char* name() const = 0;
    virtual void onFill(std::size_t frame, std::uint64_t key, std::uint64_t now) = 0;
    virtual void onAccess(std::size_t frame, std::uint64_t key, std::uint64_t now) = 0;
    virtual void onRemove(std::size_t frame) = 0;
    virtual std::size_t evict(const std::vector<std::size_t>& candidates) = 0;
    virtual void setFuture(const std::vector<std::uint64_t>&) {}
    virtual void tick(std::uint64_t) {}
};

class FifoPolicy : public ReplacementPolicy {
public:
    const char* name() const override { return "FIFO"; }

    void onFill(std::size_t frame, std::uint64_t, std::uint64_t) override {
        arrival_[frame] = ++counter_;
    }

    void onAccess(std::size_t, std::uint64_t, std::uint64_t) override {}

    void onRemove(std::size_t frame) override { arrival_.erase(frame); }

    std::size_t evict(const std::vector<std::size_t>& candidates) override {
        if (candidates.empty()) {
            throw OutOfFrames("no eviction candidate available");
        }
        std::size_t best = candidates.front();
        std::uint64_t bestArrival = lookup(best);
        for (std::size_t frame : candidates) {
            std::uint64_t arrival = lookup(frame);
            if (arrival < bestArrival) {
                best = frame;
                bestArrival = arrival;
            }
        }
        return best;
    }

private:
    std::uint64_t lookup(std::size_t frame) const {
        auto it = arrival_.find(frame);
        return it == arrival_.end() ? 0 : it->second;
    }

    std::unordered_map<std::size_t, std::uint64_t> arrival_;
    std::uint64_t counter_ = 0;
};

class LruPolicy : public ReplacementPolicy {
public:
    const char* name() const override { return "LRU"; }

    void onFill(std::size_t frame, std::uint64_t, std::uint64_t now) override {
        lastUse_[frame] = now;
    }

    void onAccess(std::size_t frame, std::uint64_t, std::uint64_t now) override {
        lastUse_[frame] = now;
    }

    void onRemove(std::size_t frame) override { lastUse_.erase(frame); }

    std::size_t evict(const std::vector<std::size_t>& candidates) override {
        if (candidates.empty()) {
            throw OutOfFrames("no eviction candidate available");
        }
        std::size_t best = candidates.front();
        std::uint64_t bestUse = lookup(best);
        for (std::size_t frame : candidates) {
            std::uint64_t use = lookup(frame);
            if (use < bestUse) {
                best = frame;
                bestUse = use;
            }
        }
        return best;
    }

private:
    std::uint64_t lookup(std::size_t frame) const {
        auto it = lastUse_.find(frame);
        return it == lastUse_.end() ? 0 : it->second;
    }

    std::unordered_map<std::size_t, std::uint64_t> lastUse_;
};

class ClockPolicy : public ReplacementPolicy {
public:
    explicit ClockPolicy(std::size_t frameCount)
        : referenceBit_(frameCount, false) {}

    const char* name() const override { return "CLOCK"; }

    void onFill(std::size_t frame, std::uint64_t, std::uint64_t) override {
        referenceBit_[frame] = true;
    }

    void onAccess(std::size_t frame, std::uint64_t, std::uint64_t) override {
        referenceBit_[frame] = true;
    }

    void onRemove(std::size_t frame) override {
        referenceBit_[frame] = false;
    }

    std::size_t evict(const std::vector<std::size_t>& candidates) override {
        if (candidates.empty()) {
            throw OutOfFrames("no eviction candidate available");
        }
        std::vector<bool> eligible(referenceBit_.size(), false);
        for (std::size_t frame : candidates) {
            eligible[frame] = true;
        }
        std::size_t scanned = 0;
        std::size_t limit = referenceBit_.size() * 2 + candidates.size();
        while (scanned <= limit) {
            std::size_t frame = hand_;
            hand_ = (hand_ + 1) % referenceBit_.size();
            ++scanned;
            if (!eligible[frame]) {
                continue;
            }
            if (referenceBit_[frame]) {
                referenceBit_[frame] = false;
                ++secondChances_;
                continue;
            }
            return frame;
        }
        return candidates.front();
    }

    std::uint64_t secondChances() const { return secondChances_; }
    std::size_t hand() const { return hand_; }

private:
    std::vector<bool> referenceBit_;
    std::size_t hand_ = 0;
    std::uint64_t secondChances_ = 0;
};

class WorkingSetClockPolicy : public ReplacementPolicy {
public:
    WorkingSetClockPolicy(std::size_t frameCount, std::uint64_t tau)
        : referenceBit_(frameCount, false), lastUse_(frameCount, 0), tau_(tau) {}

    const char* name() const override { return "WSCLOCK"; }

    void onFill(std::size_t frame, std::uint64_t, std::uint64_t now) override {
        referenceBit_[frame] = true;
        lastUse_[frame] = now;
        now_ = now;
    }

    void onAccess(std::size_t frame, std::uint64_t, std::uint64_t now) override {
        referenceBit_[frame] = true;
        lastUse_[frame] = now;
        now_ = now;
    }

    void onRemove(std::size_t frame) override {
        referenceBit_[frame] = false;
        lastUse_[frame] = 0;
    }

    void tick(std::uint64_t now) override { now_ = now; }

    std::size_t evict(const std::vector<std::size_t>& candidates) override {
        if (candidates.empty()) {
            throw OutOfFrames("no eviction candidate available");
        }
        std::vector<bool> eligible(referenceBit_.size(), false);
        for (std::size_t frame : candidates) {
            eligible[frame] = true;
        }
        std::size_t oldest = candidates.front();
        std::size_t steps = referenceBit_.size() * 2;
        for (std::size_t i = 0; i < steps; ++i) {
            std::size_t frame = hand_;
            hand_ = (hand_ + 1) % referenceBit_.size();
            if (!eligible[frame]) {
                continue;
            }
            if (referenceBit_[frame]) {
                referenceBit_[frame] = false;
                lastUse_[frame] = now_;
                continue;
            }
            if (now_ - lastUse_[frame] > tau_) {
                ++agedOut_;
                return frame;
            }
            if (lastUse_[frame] < lastUse_[oldest]) {
                oldest = frame;
            }
        }
        for (std::size_t frame : candidates) {
            if (lastUse_[frame] < lastUse_[oldest]) {
                oldest = frame;
            }
        }
        return oldest;
    }

    std::uint64_t agedOut() const { return agedOut_; }

private:
    std::vector<bool> referenceBit_;
    std::vector<std::uint64_t> lastUse_;
    std::uint64_t tau_;
    std::uint64_t now_ = 0;
    std::size_t hand_ = 0;
    std::uint64_t agedOut_ = 0;
};

class OptimalPolicy : public ReplacementPolicy {
public:
    const char* name() const override { return "OPT"; }

    void setFuture(const std::vector<std::uint64_t>& keys) override {
        occurrences_.clear();
        for (std::size_t i = 0; i < keys.size(); ++i) {
            occurrences_[keys[i]].push_back(i);
        }
    }

    void onFill(std::size_t frame, std::uint64_t key, std::uint64_t now) override {
        frameKey_[frame] = key;
        now_ = now;
    }

    void onAccess(std::size_t frame, std::uint64_t key, std::uint64_t now) override {
        frameKey_[frame] = key;
        now_ = now;
    }

    void onRemove(std::size_t frame) override { frameKey_.erase(frame); }

    void tick(std::uint64_t now) override { now_ = now; }

    std::size_t evict(const std::vector<std::size_t>& candidates) override {
        if (candidates.empty()) {
            throw OutOfFrames("no eviction candidate available");
        }
        std::size_t best = candidates.front();
        std::uint64_t bestDistance = 0;
        bool first = true;
        for (std::size_t frame : candidates) {
            std::uint64_t distance = nextUse(frame);
            if (first || distance > bestDistance) {
                best = frame;
                bestDistance = distance;
                first = false;
            }
        }
        return best;
    }

private:
    std::uint64_t nextUse(std::size_t frame) const {
        auto keyIt = frameKey_.find(frame);
        if (keyIt == frameKey_.end()) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        auto it = occurrences_.find(keyIt->second);
        if (it == occurrences_.end()) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        const std::vector<std::size_t>& positions = it->second;
        auto pos = std::upper_bound(positions.begin(), positions.end(), static_cast<std::size_t>(now_));
        if (pos == positions.end()) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return static_cast<std::uint64_t>(*pos);
    }

    std::unordered_map<std::uint64_t, std::vector<std::size_t>> occurrences_;
    std::unordered_map<std::size_t, std::uint64_t> frameKey_;
    std::uint64_t now_ = 0;
};

inline std::unique_ptr<ReplacementPolicy> makePolicy(const std::string& name, std::size_t frameCount,
                                                     std::uint64_t tau = 64) {
    if (name == "FIFO") {
        return std::unique_ptr<ReplacementPolicy>(new FifoPolicy());
    }
    if (name == "LRU") {
        return std::unique_ptr<ReplacementPolicy>(new LruPolicy());
    }
    if (name == "CLOCK") {
        return std::unique_ptr<ReplacementPolicy>(new ClockPolicy(frameCount));
    }
    if (name == "WSCLOCK") {
        return std::unique_ptr<ReplacementPolicy>(new WorkingSetClockPolicy(frameCount, tau));
    }
    if (name == "OPT") {
        return std::unique_ptr<ReplacementPolicy>(new OptimalPolicy());
    }
    throw PagingError("unknown replacement policy: " + name);
}

}
