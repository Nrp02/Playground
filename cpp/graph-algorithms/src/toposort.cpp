#include "toposort.hpp"

#include <queue>

std::vector<int> topoSort(const Graph& g, bool& hasCycle) {
    int n = g.numVertices();
    std::vector<int> inDegree(n, 0);

    for (int u = 0; u < n; u++) {
        for (const auto& e : g.neighbors(u)) {
            inDegree[e.to]++;
        }
    }

    std::queue<int> q;
    for (int v = 0; v < n; v++) {
        if (inDegree[v] == 0) q.push(v);
    }

    std::vector<int> order;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        order.push_back(u);
        for (const auto& e : g.neighbors(u)) {
            if (--inDegree[e.to] == 0) q.push(e.to);
        }
    }

    hasCycle = static_cast<int>(order.size()) != n;
    return order;
}
