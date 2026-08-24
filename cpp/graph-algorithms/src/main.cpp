#include "astar.hpp"
#include "bfs_dfs.hpp"
#include "dijkstra.hpp"
#include "graph.hpp"
#include "mst.hpp"
#include "scc.hpp"
#include "toposort.hpp"

#include <iostream>
#include <string>

namespace {

void printPath(const std::vector<int>& path) {
    if (path.empty()) {
        std::cout << "no path found\n";
        return;
    }
    for (size_t i = 0; i < path.size(); i++) {
        std::cout << path[i];
        if (i + 1 < path.size()) std::cout << " -> ";
    }
    std::cout << "\n";
}

int usage(const char* prog) {
    std::cerr << "usage: " << prog << " <graphfile> <command> [args...]\n"
              << "commands:\n"
              << "  bfs <start> <target>\n"
              << "  dfs <start> <target>\n"
              << "  dijkstra <start> <target>\n"
              << "  astar <start> <target>\n"
              << "  mst-kruskal\n"
              << "  mst-prim\n"
              << "  topo-sort\n"
              << "  scc\n";
    return 1;
}

}

int main(int argc, char** argv) {
    if (argc < 3) return usage(argv[0]);

    std::string graphFile = argv[1];
    std::string cmd = argv[2];

    Graph g = Graph::loadFromFile(graphFile);

    if (cmd == "bfs" || cmd == "dfs") {
        if (argc < 5) return usage(argv[0]);
        int start = std::stoi(argv[3]);
        int target = std::stoi(argv[4]);
        auto path = (cmd == "bfs") ? bfsPath(g, start, target) : dfsPath(g, start, target);
        printPath(path);
    } else if (cmd == "dijkstra") {
        if (argc < 5) return usage(argv[0]);
        int start = std::stoi(argv[3]);
        int target = std::stoi(argv[4]);
        auto result = dijkstra(g, start);
        if (result.dist[target] == kInfinity) {
            std::cout << "no path found\n";
        } else {
            std::cout << "cost: " << result.dist[target] << "\n";
            printPath(reconstructPath(result.parent, start, target));
        }
    } else if (cmd == "astar") {
        if (argc < 5) return usage(argv[0]);
        int start = std::stoi(argv[3]);
        int target = std::stoi(argv[4]);
        Heuristic h = g.hasCoords() ? euclideanHeuristic : zeroHeuristic;
        auto result = astar(g, start, target, h);
        if (!result.found) {
            std::cout << "no path found\n";
        } else {
            std::cout << "cost: " << result.cost << "\n";
            printPath(result.path);
        }
    } else if (cmd == "mst-kruskal") {
        auto result = kruskalMST(g);
        for (const auto& [u, v, w] : result.edges) {
            std::cout << u << " -- " << v << " (" << w << ")\n";
        }
        std::cout << "total weight: " << result.totalWeight << "\n";
        std::cout << "edges: " << result.edges.size() << " / " << g.numVertices() - 1 << "\n";
    } else if (cmd == "mst-prim") {
        auto result = primMST(g);
        for (const auto& [u, v, w] : result.edges) {
            std::cout << u << " -- " << v << " (" << w << ")\n";
        }
        std::cout << "total weight: " << result.totalWeight << "\n";
        std::cout << "edges: " << result.edges.size() << " / " << g.numVertices() - 1 << "\n";
    } else if (cmd == "topo-sort") {
        bool hasCycle = false;
        auto order = topoSort(g, hasCycle);
        if (hasCycle) {
            std::cout << "graph has a cycle, no valid topological order\n";
        } else {
            printPath(order);
        }
    } else if (cmd == "scc") {
        auto components = tarjanSCC(g);
        std::cout << components.size() << " strongly connected component(s)\n";
        for (const auto& comp : components) {
            std::cout << "{ ";
            for (int v : comp) std::cout << v << " ";
            std::cout << "}\n";
        }
    } else {
        return usage(argv[0]);
    }

    return 0;
}
