#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../src/maxflow.hpp"

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
        std::cerr << "FAIL: " << testName << " (got " << actual << ", expected " << expected << ")\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

struct RandomEdgeGraph {
    int n;
    std::vector<std::pair<int, int>> edgeEndpoints;
    std::vector<mf::Flow> caps;
};

RandomEdgeGraph makeRandomGraph(std::mt19937& rng, int n, int edgeAttempts, mf::Flow maxCap) {
    std::uniform_int_distribution<int> vertexDist(0, n - 1);
    std::uniform_int_distribution<mf::Flow> capDist(0, maxCap);
    RandomEdgeGraph g;
    g.n = n;
    for (int i = 0; i < edgeAttempts; ++i) {
        int u = vertexDist(rng);
        int v = vertexDist(rng);
        mf::Flow cap = capDist(rng);
        g.edgeEndpoints.emplace_back(u, v);
        g.caps.push_back(cap);
    }
    return g;
}

mf::Flow bruteForceMaxFlow(const RandomEdgeGraph& g, int s, int t) {
    int m = static_cast<int>(g.edgeEndpoints.size());
    std::vector<std::vector<mf::Flow>> cap(static_cast<std::size_t>(g.n), std::vector<mf::Flow>(static_cast<std::size_t>(g.n), 0));
    for (int i = 0; i < m; ++i) {
        cap[static_cast<std::size_t>(g.edgeEndpoints[static_cast<std::size_t>(i)].first)][static_cast<std::size_t>(g.edgeEndpoints[static_cast<std::size_t>(i)].second)] += g.caps[static_cast<std::size_t>(i)];
    }
    mf::Flow total = 0;
    while (true) {
        std::vector<int> parent(static_cast<std::size_t>(g.n), -1);
        parent[static_cast<std::size_t>(s)] = s;
        std::vector<int> queue{s};
        std::size_t head = 0;
        while (head < queue.size() && parent[static_cast<std::size_t>(t)] == -1) {
            int u = queue[head++];
            for (int v = 0; v < g.n; ++v) {
                if (parent[static_cast<std::size_t>(v)] == -1 && cap[static_cast<std::size_t>(u)][static_cast<std::size_t>(v)] > 0) {
                    parent[static_cast<std::size_t>(v)] = u;
                    queue.push_back(v);
                }
            }
        }
        if (parent[static_cast<std::size_t>(t)] == -1) {
            break;
        }
        mf::Flow bottleneck = mf::kInfFlow;
        int v = t;
        while (v != s) {
            int u = parent[static_cast<std::size_t>(v)];
            bottleneck = std::min(bottleneck, cap[static_cast<std::size_t>(u)][static_cast<std::size_t>(v)]);
            v = u;
        }
        v = t;
        while (v != s) {
            int u = parent[static_cast<std::size_t>(v)];
            cap[static_cast<std::size_t>(u)][static_cast<std::size_t>(v)] -= bottleneck;
            cap[static_cast<std::size_t>(v)][static_cast<std::size_t>(u)] += bottleneck;
            v = u;
        }
        total += bottleneck;
    }
    return total;
}

mf::Flow dinicMaxFlowFromGraph(const RandomEdgeGraph& g, int s, int t, mf::MaxFlow* outFlow) {
    mf::MaxFlow flow(g.n);
    for (std::size_t i = 0; i < g.edgeEndpoints.size(); ++i) {
        flow.addEdge(g.edgeEndpoints[i].first, g.edgeEndpoints[i].second, g.caps[i]);
    }
    mf::Flow value = flow.maxflow(s, t);
    if (outFlow != nullptr) {
        *outFlow = flow;
    }
    return value;
}

bool checkFeasibilityInvariants(const mf::MaxFlow& flow, int s, int t) {
    int n = flow.numVertices();
    std::vector<mf::Flow> net(static_cast<std::size_t>(n), 0);
    for (int id = 0; id < flow.numEdges(); id += 2) {
        mf::Flow f = flow.edgeFlow(id);
        mf::Flow c = flow.edgeCap(id);
        if (f < 0 || f > c) {
            return false;
        }
        mf::Flow reverseFlow = flow.edgeFlow(id + 1);
        if (reverseFlow != -f) {
            return false;
        }
        net[static_cast<std::size_t>(flow.edgeFrom(id))] -= f;
        net[static_cast<std::size_t>(flow.edgeTo(id))] += f;
    }
    for (int v = 0; v < n; ++v) {
        if (v == s || v == t) {
            continue;
        }
        if (net[static_cast<std::size_t>(v)] != 0) {
            return false;
        }
    }
    return true;
}

std::vector<std::pair<int, int>> bruteForceBipartiteMatching(int leftN, int rightN,
                                                               const std::vector<std::pair<int, int>>& edges) {
    std::size_t m = edges.size();
    std::size_t bestSize = 0;
    for (std::size_t mask = 0; mask < (std::size_t{1} << m); ++mask) {
        std::vector<bool> leftUsed(static_cast<std::size_t>(leftN), false);
        std::vector<bool> rightUsed(static_cast<std::size_t>(rightN), false);
        std::size_t count = 0;
        bool valid = true;
        for (std::size_t i = 0; i < m && valid; ++i) {
            if ((mask >> i) & 1u) {
                int l = edges[i].first;
                int r = edges[i].second;
                if (leftUsed[static_cast<std::size_t>(l)] || rightUsed[static_cast<std::size_t>(r)]) {
                    valid = false;
                    break;
                }
                leftUsed[static_cast<std::size_t>(l)] = true;
                rightUsed[static_cast<std::size_t>(r)] = true;
                ++count;
            }
        }
        if (valid) {
            bestSize = std::max(bestSize, count);
        }
    }
    return std::vector<std::pair<int, int>>(bestSize);
}

void testKnownNetwork() {
    mf::MaxFlow flow(6);
    flow.addEdge(0, 1, 16);
    flow.addEdge(0, 2, 13);
    flow.addEdge(1, 2, 10);
    flow.addEdge(2, 1, 4);
    flow.addEdge(1, 3, 12);
    flow.addEdge(3, 2, 9);
    flow.addEdge(2, 4, 14);
    flow.addEdge(4, 3, 7);
    flow.addEdge(3, 5, 20);
    flow.addEdge(4, 5, 4);
    mf::Flow value = flow.maxflow(0, 5);
    expectEq<mf::Flow>(value, 23, "known network max flow value");
    expectTrue(checkFeasibilityInvariants(flow, 0, 5), "known network feasibility invariants");
    expectEq<mf::Flow>(flow.minCutCapacity(0), value, "known network min cut equals max flow");
}

void testRandomizedAgainstBruteForce() {
    std::mt19937 rng(123);
    const int trials = 60;
    bool allValueMatches = true;
    bool allFeasible = true;
    bool allCutMatches = true;
    for (int trial = 0; trial < trials; ++trial) {
        std::uniform_int_distribution<int> nDist(2, 6);
        int n = nDist(rng);
        std::uniform_int_distribution<int> edgeCountDist(0, 10);
        int edgeAttempts = edgeCountDist(rng);
        RandomEdgeGraph g = makeRandomGraph(rng, n, edgeAttempts, 8);
        std::uniform_int_distribution<int> vertexDist(0, n - 1);
        int s = vertexDist(rng);
        int t = vertexDist(rng);
        if (s == t) {
            continue;
        }
        mf::MaxFlow flow(0);
        mf::Flow dinicValue = dinicMaxFlowFromGraph(g, s, t, &flow);
        mf::Flow bruteValue = bruteForceMaxFlow(g, s, t);
        if (dinicValue != bruteValue) {
            allValueMatches = false;
        }
        if (!checkFeasibilityInvariants(flow, s, t)) {
            allFeasible = false;
        }
        if (flow.minCutCapacity(s) != dinicValue) {
            allCutMatches = false;
        }
    }
    expectTrue(allValueMatches, "randomized dinic matches brute-force max flow on all trials");
    expectTrue(allFeasible, "randomized dinic satisfies feasibility invariants on all trials");
    expectTrue(allCutMatches, "randomized dinic min cut equals max flow on all trials");
}

void testBipartiteMatchingAgainstBruteForce() {
    std::mt19937 rng(99);
    const int trials = 20;
    bool allSizesMatch = true;
    bool allCoversValid = true;
    for (int trial = 0; trial < trials; ++trial) {
        int leftN = 4;
        int rightN = 4;
        std::uniform_int_distribution<int> includeDist(0, 1);
        std::set<std::pair<int, int>> edgeSet;
        for (int l = 0; l < leftN; ++l) {
            for (int r = 0; r < rightN; ++r) {
                if (includeDist(rng) == 1) {
                    edgeSet.emplace(l, r);
                }
            }
        }
        std::vector<std::pair<int, int>> edges(edgeSet.begin(), edgeSet.end());
        mf::BipartiteMatching result = mf::bipartiteMaxMatching(leftN, rightN, edges);
        std::vector<std::pair<int, int>> bruteMatching = bruteForceBipartiteMatching(leftN, rightN, edges);
        if (result.matches.size() != bruteMatching.size()) {
            allSizesMatch = false;
        }
        std::size_t coverSize = result.vertexCoverLeft.size() + result.vertexCoverRight.size();
        if (coverSize != result.matches.size()) {
            allCoversValid = false;
        }
        std::set<int> leftCoverSet(result.vertexCoverLeft.begin(), result.vertexCoverLeft.end());
        std::set<int> rightCoverSet(result.vertexCoverRight.begin(), result.vertexCoverRight.end());
        for (const auto& [l, r] : edges) {
            if (leftCoverSet.find(l) == leftCoverSet.end() && rightCoverSet.find(r) == rightCoverSet.end()) {
                allCoversValid = false;
            }
        }
    }
    expectTrue(allSizesMatch, "bipartite matching size matches brute force on all trials");
    expectTrue(allCoversValid, "konig vertex cover is valid and equal in size to matching on all trials");
}

void testEdgeCases() {
    {
        mf::MaxFlow flow(3);
        flow.addEdge(0, 1, 5);
        flow.addEdge(1, 2, 5);
        mf::Flow value = flow.maxflow(0, 0);
        expectEq<mf::Flow>(value, 0, "source equals sink yields zero flow");
    }
    {
        mf::MaxFlow flow(4);
        flow.addEdge(0, 1, 5);
        flow.addEdge(2, 3, 5);
        mf::Flow value = flow.maxflow(0, 3);
        expectEq<mf::Flow>(value, 0, "disconnected source and sink yields zero flow");
    }
    {
        mf::MaxFlow flow(3);
        flow.addEdge(0, 1, 0);
        flow.addEdge(1, 2, 10);
        mf::Flow value = flow.maxflow(0, 2);
        expectEq<mf::Flow>(value, 0, "zero-capacity edge blocks flow");
    }
    {
        mf::MaxFlow flow(2);
        flow.addEdge(0, 1, 3);
        flow.addEdge(0, 1, 4);
        mf::Flow value = flow.maxflow(0, 1);
        expectEq<mf::Flow>(value, 7, "parallel edges sum capacities");
    }
    {
        mf::MaxFlow flow(2);
        flow.addEdge(0, 0, 10);
        flow.addEdge(0, 1, 5);
        mf::Flow value = flow.maxflow(0, 1);
        expectEq<mf::Flow>(value, 5, "self loop does not affect max flow");
    }
}

void testEdgeDisjointPaths() {
    int n = 6;
    std::vector<std::pair<int, int>> edges{
        {0, 1}, {0, 2}, {1, 3}, {2, 3}, {3, 4}, {3, 5}, {4, 5}, {0, 5}};
    int paths = mf::edgeDisjointPaths(n, edges, 0, 5, true);
    expectTrue(paths >= 2, "edge disjoint paths finds at least two independent routes");

    std::vector<std::pair<int, int>> triangle{{0, 1}, {1, 2}, {0, 2}};
    int trianglePaths = mf::edgeDisjointPaths(3, triangle, 0, 2, true);
    expectEq<int>(trianglePaths, 2, "edge disjoint paths on triangle graph");
}

}

int main() {
    testKnownNetwork();
    testRandomizedAgainstBruteForce();
    testBipartiteMatchingAgainstBruteForce();
    testEdgeCases();
    testEdgeDisjointPaths();

    std::cout << "\n" << g_failures << " failing test(s)\n";
    return g_failures == 0 ? 0 : 1;
}
