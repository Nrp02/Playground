#pragma once

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "consistent_hash_ring.hpp"

namespace chr {

class LoadBalancer {
public:
    explicit LoadBalancer(int virtualNodesPerNode = 150) : ring_(virtualNodesPerNode) {}

    void addServer(const std::string& serverId) {
        ring_.addNode(serverId);
        stats_.emplace(serverId, 0);
    }

    void removeServer(const std::string& serverId) {
        ring_.removeNode(serverId);
        stats_.erase(serverId);
    }

    std::string routeRequest(const std::string& key) {
        std::string serverId = ring_.route(key);
        ++stats_[serverId];
        return serverId;
    }

    const std::map<std::string, std::size_t>& distribution() const {
        return stats_;
    }

    void resetStats() {
        for (auto& entry : stats_) {
            entry.second = 0;
        }
    }

    std::size_t serverCount() const {
        return ring_.nodeCount();
    }

    const ConsistentHashRing& ring() const {
        return ring_;
    }

private:
    ConsistentHashRing ring_;
    std::map<std::string, std::size_t> stats_;
};

inline double distributionStdDevPercent(const std::map<std::string, std::size_t>& distribution, std::size_t totalRequests) {
    if (distribution.empty() || totalRequests == 0) {
        return 0.0;
    }
    double mean = static_cast<double>(totalRequests) / static_cast<double>(distribution.size());
    double variance = 0.0;
    for (const auto& entry : distribution) {
        double diff = static_cast<double>(entry.second) - mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(distribution.size());
    return (std::sqrt(variance) / mean) * 100.0;
}

inline double remapPercentage(const ConsistentHashRing& before, const ConsistentHashRing& after,
                               const std::vector<std::string>& sampleKeys) {
    if (sampleKeys.empty()) {
        return 0.0;
    }
    std::size_t changed = 0;
    for (const auto& key : sampleKeys) {
        if (before.route(key) != after.route(key)) {
            ++changed;
        }
    }
    return 100.0 * static_cast<double>(changed) / static_cast<double>(sampleKeys.size());
}

}
