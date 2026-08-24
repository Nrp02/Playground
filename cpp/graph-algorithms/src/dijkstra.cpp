#include "dijkstra.hpp"

#include <algorithm>
#include <queue>

DijkstraResult dijkstra(const Graph& g, int start) {
    int n = g.numVertices();
    DijkstraResult result;
    result.dist.assign(n, kInfinity);
    result.parent.assign(n, -2);

    using PQItem = std::pair<double, int>;
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;

    result.dist[start] = 0.0;
    result.parent[start] = -1;
    pq.push({0.0, start});

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();
        if (d > result.dist[u]) continue;

        for (const auto& e : g.neighbors(u)) {
            double nd = d + e.weight;
            if (nd < result.dist[e.to]) {
                result.dist[e.to] = nd;
                result.parent[e.to] = u;
                pq.push({nd, e.to});
            }
        }
    }

    return result;
}

std::vector<int> reconstructPath(const std::vector<int>& parent, int start, int target) {
    if (parent[target] == -2) return {};
    std::vector<int> path;
    int cur = target;
    while (cur != -1) {
        path.push_back(cur);
        if (cur == start) break;
        cur = parent[cur];
    }
    std::reverse(path.begin(), path.end());
    if (path.empty() || path.front() != start) return {};
    return path;
}
