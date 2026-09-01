#include <chrono>
#include <iostream>
#include <random>
#include <set>
#include <vector>

#include "maxflow.hpp"

namespace {

void runKnownNetworkDemo() {
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
    std::cout << "Known network max flow: " << value << " (expected 23)\n";

    auto cutEdges = flow.minCutEdges(0);
    std::cout << "Min cut edges:\n";
    for (const auto& [u, v] : cutEdges) {
        std::cout << "  " << u << " -> " << v << "\n";
    }
    mf::Flow cutCapacity = flow.minCutCapacity(0);
    std::cout << "Min cut capacity: " << cutCapacity << (cutCapacity == value ? " matches max flow\n" : " MISMATCH\n");
}

void runBipartiteMatchingDemo() {
    const int leftN = 40;
    const int rightN = 40;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> rightPick(0, rightN - 1);
    std::uniform_int_distribution<int> degreeDist(2, 5);

    std::set<std::pair<int, int>> edgeSet;
    for (int l = 0; l < leftN; ++l) {
        int degree = degreeDist(rng);
        for (int k = 0; k < degree; ++k) {
            edgeSet.emplace(l, rightPick(rng));
        }
    }
    std::vector<std::pair<int, int>> edges(edgeSet.begin(), edgeSet.end());

    mf::BipartiteMatching result = mf::bipartiteMaxMatching(leftN, rightN, edges);
    std::size_t coverSize = result.vertexCoverLeft.size() + result.vertexCoverRight.size();

    std::cout << "\nBipartite graph: " << leftN << " x " << rightN << " with " << edges.size() << " edges\n";
    std::cout << "Maximum matching size: " << result.matches.size() << "\n";
    std::cout << "Konig minimum vertex cover size: " << coverSize
               << (coverSize == result.matches.size() ? " matches matching size\n" : " MISMATCH\n");
}

void runTimingDemo() {
    const int n = 4000;
    const int edgeCount = 40000;
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> vertexDist(0, n - 1);
    std::uniform_int_distribution<mf::Flow> capDist(1, 1000);

    mf::MaxFlow flow(n);
    for (int i = 0; i < edgeCount; ++i) {
        int u = vertexDist(rng);
        int v = vertexDist(rng);
        if (u == v) {
            continue;
        }
        flow.addEdge(u, v, capDist(rng));
    }

    auto start = std::chrono::steady_clock::now();
    mf::Flow value = flow.maxflow(0, n - 1);
    auto end = std::chrono::steady_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "\nLarge random graph: " << n << " vertices, " << edgeCount << " candidate edges\n";
    std::cout << "Max flow value: " << value << "\n";
    std::cout << "Elapsed time: " << elapsedMs << " ms\n";
}

}

int main() {
    runKnownNetworkDemo();
    runBipartiteMatchingDemo();
    runTimingDemo();
    return 0;
}
