#include "astar.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

double euclideanHeuristic(const Graph& g, int a, int b) {
    if (!g.hasCoords()) return 0.0;
    auto [ax, ay] = g.coords(a);
    auto [bx, by] = g.coords(b);
    return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

double zeroHeuristic(const Graph&, int, int) { return 0.0; }

AStarResult astar(const Graph& g, int start, int goal, const Heuristic& h) {
    int n = g.numVertices();
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> gScore(n, inf);
    std::vector<int> parent(n, -2);

    using PQItem = std::pair<double, int>;
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> open;

    gScore[start] = 0.0;
    parent[start] = -1;
    open.push({h(g, start, goal), start});

    std::vector<bool> closed(n, false);

    while (!open.empty()) {
        auto [f, u] = open.top();
        open.pop();
        (void)f;
        if (closed[u]) continue;
        closed[u] = true;

        if (u == goal) break;

        for (const auto& e : g.neighbors(u)) {
            double tentative = gScore[u] + e.weight;
            if (tentative < gScore[e.to]) {
                gScore[e.to] = tentative;
                parent[e.to] = u;
                open.push({tentative + h(g, e.to, goal), e.to});
            }
        }
    }

    AStarResult result;
    if (gScore[goal] == inf) {
        result.found = false;
        result.cost = inf;
        return result;
    }

    std::vector<int> path;
    int cur = goal;
    while (cur != -1) {
        path.push_back(cur);
        if (cur == start) break;
        cur = parent[cur];
    }
    std::reverse(path.begin(), path.end());

    result.found = true;
    result.cost = gScore[goal];
    result.path = path;
    return result;
}
