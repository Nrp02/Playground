#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace chr {

inline std::uint64_t fnv1a(const std::string& data) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : data) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline std::uint64_t avalanche(std::uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

inline std::uint64_t ringHash(const std::string& data) {
    return avalanche(fnv1a(data));
}

class ConsistentHashRing {
public:
    explicit ConsistentHashRing(int virtualNodesPerNode = 150)
        : virtualNodesPerNode_(virtualNodesPerNode) {
        if (virtualNodesPerNode_ <= 0) {
            throw std::invalid_argument("virtualNodesPerNode must be > 0");
        }
    }

    void addNode(const std::string& nodeId) {
        if (nodes_.count(nodeId) > 0) {
            return;
        }
        nodes_.insert(nodeId);
        for (int i = 0; i < virtualNodesPerNode_; ++i) {
            std::uint64_t hash = ringHash(nodeId + "#" + std::to_string(i));
            ring_[hash] = nodeId;
        }
    }

    void removeNode(const std::string& nodeId) {
        if (nodes_.count(nodeId) == 0) {
            return;
        }
        nodes_.erase(nodeId);
        for (int i = 0; i < virtualNodesPerNode_; ++i) {
            std::uint64_t hash = ringHash(nodeId + "#" + std::to_string(i));
            ring_.erase(hash);
        }
    }

    std::string route(const std::string& key) const {
        if (ring_.empty()) {
            throw std::runtime_error("cannot route: ring has no nodes");
        }
        std::uint64_t hash = ringHash(key);
        auto it = ring_.lower_bound(hash);
        if (it == ring_.end()) {
            it = ring_.begin();
        }
        return it->second;
    }

    std::size_t nodeCount() const {
        return nodes_.size();
    }

    std::vector<std::string> nodes() const {
        return std::vector<std::string>(nodes_.begin(), nodes_.end());
    }

    bool hasNode(const std::string& nodeId) const {
        return nodes_.count(nodeId) > 0;
    }

private:
    int virtualNodesPerNode_;
    std::map<std::uint64_t, std::string> ring_;
    std::set<std::string> nodes_;
};

}
