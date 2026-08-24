#include "mst.hpp"

#include "union_find.hpp"

#include <algorithm>
#include <queue>

MSTResult kruskalMST(const Graph& g) {
    auto edges = g.edgeList();
    std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
        return std::get<2>(a) < std::get<2>(b);
    });

    UnionFind uf(g.numVertices());
    MSTResult result;
    result.totalWeight = 0.0;

    for (const auto& [u, v, w] : edges) {
        if (uf.unite(u, v)) {
            result.edges.emplace_back(u, v, w);
            result.totalWeight += w;
        }
    }

    return result;
}

MSTResult primMST(const Graph& g) {
    int n = g.numVertices();
    std::vector<bool> inMST(n, false);

    using PQItem = std::tuple<double, int, int>;
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;

    MSTResult result;
    result.totalWeight = 0.0;

    if (n == 0) return result;

    inMST[0] = true;
    for (const auto& e : g.neighbors(0)) {
        pq.push({e.weight, 0, e.to});
    }

    int included = 1;
    while (!pq.empty() && included < n) {
        auto [w, u, v] = pq.top();
        pq.pop();
        if (inMST[v]) continue;

        inMST[v] = true;
        result.edges.emplace_back(u, v, w);
        result.totalWeight += w;
        included++;

        for (const auto& e : g.neighbors(v)) {
            if (!inMST[e.to]) pq.push({e.weight, v, e.to});
        }
    }

    return result;
}
