#include <iostream>
#include <string>
#include <vector>

#include "../src/load_balancer.hpp"

namespace {

int g_failures = 0;

void expectTrue(bool condition, const std::string& testName) {
    if (!condition) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

template <typename T>
void expectEq(const T& actual, const T& expected, const std::string& testName) {
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

std::vector<std::string> sampleKeys(int count) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (int i = 0; i < count; ++i) {
        keys.push_back("key-" + std::to_string(i));
    }
    return keys;
}

}

int main() {
    {
        chr::ConsistentHashRing ring;
        bool threw = false;
        try {
            ring.route("anything");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        expectTrue(threw, "routing on an empty ring throws");
    }

    {
        chr::ConsistentHashRing ring;
        ring.addNode("a");
        ring.addNode("b");
        ring.addNode("c");
        expectEq<std::size_t>(ring.nodeCount(), 3, "nodeCount reflects added nodes");

        auto keys = sampleKeys(500);
        bool stable = true;
        for (const auto& key : keys) {
            if (ring.route(key) != ring.route(key)) {
                stable = false;
            }
        }
        expectTrue(stable, "routing the same key twice yields the same node");
    }

    {
        chr::ConsistentHashRing ring;
        ring.addNode("a");
        ring.addNode("b");
        expectTrue(ring.hasNode("a"), "hasNode true for added node");
        ring.removeNode("a");
        expectTrue(!ring.hasNode("a"), "hasNode false after removal");
        expectEq<std::size_t>(ring.nodeCount(), 1, "nodeCount decreases after removal");
    }

    {
        chr::ConsistentHashRing before;
        for (int i = 0; i < 5; ++i) {
            before.addNode("server-" + std::to_string(i));
        }
        auto keys = sampleKeys(5000);

        chr::ConsistentHashRing afterAddCopy;
        for (int i = 0; i < 5; ++i) {
            afterAddCopy.addNode("server-" + std::to_string(i));
        }
        afterAddCopy.addNode("server-5");

        double remap = chr::remapPercentage(before, afterAddCopy, keys);
        expectTrue(remap < 40.0, "adding one of six nodes remaps well under 100% of keys");
        expectTrue(remap > 0.0, "adding a node does remap some keys");
    }

    {
        chr::ConsistentHashRing before;
        for (int i = 0; i < 5; ++i) {
            before.addNode("server-" + std::to_string(i));
        }
        chr::ConsistentHashRing after;
        for (int i = 0; i < 5; ++i) {
            after.addNode("server-" + std::to_string(i));
        }
        after.removeNode("server-2");

        auto keys = sampleKeys(5000);
        for (const auto& key : keys) {
            std::string beforeNode = before.route(key);
            std::string afterNode = after.route(key);
            if (beforeNode != "server-2") {
                expectEq(beforeNode, afterNode, "keys not owned by the removed node keep their server: " + key);
                break;
            }
        }
    }

    {
        chr::LoadBalancer lb(150);
        for (int i = 0; i < 4; ++i) {
            lb.addServer("s" + std::to_string(i));
        }
        auto keys = sampleKeys(20000);
        for (const auto& key : keys) {
            lb.routeRequest(key);
        }
        double stddevPercent = chr::distributionStdDevPercent(lb.distribution(), keys.size());
        expectTrue(stddevPercent < 15.0, "request distribution across 4 servers stays reasonably balanced");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
