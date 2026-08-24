#include <iostream>
#include <string>
#include <vector>

#include "load_balancer.hpp"

int main() {
    chr::LoadBalancer lb(150);
    for (int i = 0; i < 5; ++i) {
        lb.addServer("server-" + std::to_string(i));
    }

    std::vector<std::string> keys;
    keys.reserve(100000);
    for (int i = 0; i < 100000; ++i) {
        keys.push_back("request-key-" + std::to_string(i));
    }

    std::cout << "=== routing 100000 requests across 5 servers ===\n";
    for (const auto& key : keys) {
        lb.routeRequest(key);
    }
    for (const auto& entry : lb.distribution()) {
        std::cout << "  " << entry.first << ": " << entry.second << " requests\n";
    }
    std::cout << "distribution stddev: "
              << chr::distributionStdDevPercent(lb.distribution(), keys.size()) << "% of mean\n";

    chr::ConsistentHashRing before = lb.ring();

    std::cout << "\n=== adding a 6th server ===\n";
    lb.addServer("server-5");
    double addRemap = chr::remapPercentage(before, lb.ring(), keys);
    std::cout << "keys remapped after adding a node: " << addRemap << "% (naive mod hashing would remap ~100%)\n";

    before = lb.ring();
    std::cout << "\n=== removing server-2 ===\n";
    lb.removeServer("server-2");
    double removeRemap = chr::remapPercentage(before, lb.ring(), keys);
    std::cout << "keys remapped after removing a node: " << removeRemap << "% (only that node's keys should move)\n";

    lb.resetStats();
    for (const auto& key : keys) {
        lb.routeRequest(key);
    }
    std::cout << "\n=== distribution across remaining " << lb.serverCount() << " servers ===\n";
    for (const auto& entry : lb.distribution()) {
        std::cout << "  " << entry.first << ": " << entry.second << " requests\n";
    }

    return 0;
}
