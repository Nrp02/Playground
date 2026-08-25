#include <iostream>
#include <random>
#include <vector>

#include "disjoint_set_union.hpp"

namespace {

std::vector<dsu::Edge> generateRandomGraph(std::size_t vertexCount, std::size_t edgeCount, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> vertexDist(0, vertexCount - 1);
    std::uniform_int_distribution<long long> weightDist(1, 100);

    std::vector<dsu::Edge> edges;
    edges.reserve(edgeCount);
    for (std::size_t i = 1; i < vertexCount; ++i) {
        edges.push_back({i - 1, i, weightDist(rng)});
    }
    while (edges.size() < edgeCount) {
        std::size_t u = vertexDist(rng);
        std::size_t v = vertexDist(rng);
        if (u != v) {
            edges.push_back({u, v, weightDist(rng)});
        }
    }
    return edges;
}

}

int main() {
    const std::size_t vertexCount = 12;
    const std::size_t edgeCount = 30;
    std::vector<dsu::Edge> edges = generateRandomGraph(vertexCount, edgeCount, 42);

    std::cout << "Graph: " << vertexCount << " vertices, " << edges.size() << " edges\n";

    dsu::MstResult mst = dsu::kruskalMst(vertexCount, edges);

    std::cout << "Spans all vertices: " << (mst.spansAllVertices ? "yes" : "no") << "\n";
    std::cout << "MST edges chosen (" << mst.edges.size() << "):\n";
    for (const dsu::Edge& edge : mst.edges) {
        std::cout << "  " << edge.u << " -- " << edge.v << " (weight " << edge.weight << ")\n";
    }
    std::cout << "Total MST weight: " << mst.totalWeight << "\n";

    dsu::DisjointSetUnion components(vertexCount);
    std::cout << "\nUnion-Find standalone demo:\n";
    std::cout << "Initial set count: " << components.setCount() << "\n";
    components.unite(0, 1);
    components.unite(1, 2);
    components.unite(3, 4);
    std::cout << "After uniting (0,1), (1,2), (3,4): set count = " << components.setCount() << "\n";
    std::cout << "connected(0, 2) = " << (components.connected(0, 2) ? "true" : "false") << "\n";
    std::cout << "connected(0, 3) = " << (components.connected(0, 3) ? "true" : "false") << "\n";
    std::cout << "sizeOfSet(0) = " << components.sizeOfSet(0) << "\n";

    return 0;
}
