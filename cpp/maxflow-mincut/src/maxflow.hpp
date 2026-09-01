#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace mf {

using Flow = long long;

constexpr Flow kInfFlow = std::numeric_limits<Flow>::max() / 4;

struct EdgeRecord {
    int from;
    int to;
    Flow cap;
    Flow flow;
};

class MaxFlow {
public:
    explicit MaxFlow(int n) : n_(n), graph_(static_cast<std::size_t>(n)) {}

    int addEdge(int from, int to, Flow cap) {
        int id = static_cast<int>(edges_.size());
        edges_.push_back(EdgeRecord{from, to, cap, 0});
        edges_.push_back(EdgeRecord{to, from, 0, 0});
        graph_[static_cast<std::size_t>(from)].push_back(id);
        graph_[static_cast<std::size_t>(to)].push_back(id + 1);
        return id;
    }

    Flow maxflow(int s, int t) {
        if (s == t) {
            return 0;
        }
        Flow total = 0;
        while (bfs(s, t)) {
            iter_.assign(static_cast<std::size_t>(n_), 0);
            Flow f;
            while ((f = dfs(s, t, kInfFlow)) > 0) {
                total += f;
            }
        }
        return total;
    }

    std::vector<bool> minCutReachable(int s) const {
        std::vector<bool> visited(static_cast<std::size_t>(n_), false);
        std::queue<int> q;
        visited[static_cast<std::size_t>(s)] = true;
        q.push(s);
        while (!q.empty()) {
            int u = q.front();
            q.pop();
            for (int id : graph_[static_cast<std::size_t>(u)]) {
                const EdgeRecord& e = edges_[static_cast<std::size_t>(id)];
                Flow residual = e.cap - e.flow;
                if (residual > 0 && !visited[static_cast<std::size_t>(e.to)]) {
                    visited[static_cast<std::size_t>(e.to)] = true;
                    q.push(e.to);
                }
            }
        }
        return visited;
    }

    std::vector<std::pair<int, int>> minCutEdges(int s) const {
        std::vector<bool> reachable = minCutReachable(s);
        std::vector<std::pair<int, int>> cut;
        for (std::size_t i = 0; i < edges_.size(); i += 2) {
            const EdgeRecord& e = edges_[i];
            if (e.cap > 0 && reachable[static_cast<std::size_t>(e.from)] &&
                !reachable[static_cast<std::size_t>(e.to)]) {
                cut.emplace_back(e.from, e.to);
            }
        }
        return cut;
    }

    Flow minCutCapacity(int s) const {
        std::vector<bool> reachable = minCutReachable(s);
        Flow total = 0;
        for (std::size_t i = 0; i < edges_.size(); i += 2) {
            const EdgeRecord& e = edges_[i];
            if (e.cap > 0 && reachable[static_cast<std::size_t>(e.from)] &&
                !reachable[static_cast<std::size_t>(e.to)]) {
                total += e.cap;
            }
        }
        return total;
    }

    int numVertices() const { return n_; }
    int numEdges() const { return static_cast<int>(edges_.size()); }
    int edgeFrom(int id) const { return edges_[static_cast<std::size_t>(id)].from; }
    int edgeTo(int id) const { return edges_[static_cast<std::size_t>(id)].to; }
    Flow edgeCap(int id) const { return edges_[static_cast<std::size_t>(id)].cap; }
    Flow edgeFlow(int id) const { return edges_[static_cast<std::size_t>(id)].flow; }

private:
    bool bfs(int s, int t) {
        level_.assign(static_cast<std::size_t>(n_), -1);
        std::queue<int> q;
        level_[static_cast<std::size_t>(s)] = 0;
        q.push(s);
        while (!q.empty()) {
            int u = q.front();
            q.pop();
            for (int id : graph_[static_cast<std::size_t>(u)]) {
                const EdgeRecord& e = edges_[static_cast<std::size_t>(id)];
                if (e.cap - e.flow > 0 && level_[static_cast<std::size_t>(e.to)] < 0) {
                    level_[static_cast<std::size_t>(e.to)] = level_[static_cast<std::size_t>(u)] + 1;
                    q.push(e.to);
                }
            }
        }
        return level_[static_cast<std::size_t>(t)] >= 0;
    }

    Flow dfs(int u, int t, Flow pushed) {
        if (u == t || pushed == 0) {
            return pushed;
        }
        for (int& i = iter_[static_cast<std::size_t>(u)]; i < static_cast<int>(graph_[static_cast<std::size_t>(u)].size()); ++i) {
            int id = graph_[static_cast<std::size_t>(u)][static_cast<std::size_t>(i)];
            EdgeRecord& e = edges_[static_cast<std::size_t>(id)];
            if (e.cap - e.flow <= 0 || level_[static_cast<std::size_t>(e.to)] != level_[static_cast<std::size_t>(u)] + 1) {
                continue;
            }
            Flow got = dfs(e.to, t, std::min(pushed, e.cap - e.flow));
            if (got > 0) {
                e.flow += got;
                edges_[static_cast<std::size_t>(id ^ 1)].flow -= got;
                return got;
            }
        }
        return 0;
    }

    int n_;
    std::vector<std::vector<int>> graph_;
    std::vector<EdgeRecord> edges_;
    std::vector<int> level_;
    std::vector<int> iter_;
};

struct BipartiteMatching {
    std::vector<std::pair<int, int>> matches;
    std::vector<int> vertexCoverLeft;
    std::vector<int> vertexCoverRight;
};

inline BipartiteMatching bipartiteMaxMatching(int leftN, int rightN,
                                               const std::vector<std::pair<int, int>>& edges) {
    int source = 0;
    int leftBase = 1;
    int rightBase = leftBase + leftN;
    int sink = rightBase + rightN;
    int n = sink + 1;

    MaxFlow flow(n);
    std::vector<int> edgeIds;
    edgeIds.reserve(edges.size());
    for (const auto& [l, r] : edges) {
        edgeIds.push_back(flow.addEdge(leftBase + l, rightBase + r, 1));
    }
    for (int l = 0; l < leftN; ++l) {
        flow.addEdge(source, leftBase + l, 1);
    }
    for (int r = 0; r < rightN; ++r) {
        flow.addEdge(rightBase + r, sink, 1);
    }

    flow.maxflow(source, sink);

    BipartiteMatching result;
    for (std::size_t k = 0; k < edges.size(); ++k) {
        if (flow.edgeFlow(edgeIds[k]) > 0) {
            result.matches.push_back(edges[k]);
        }
    }

    std::vector<bool> reachable = flow.minCutReachable(source);
    for (int l = 0; l < leftN; ++l) {
        if (!reachable[static_cast<std::size_t>(leftBase + l)]) {
            result.vertexCoverLeft.push_back(l);
        }
    }
    for (int r = 0; r < rightN; ++r) {
        if (reachable[static_cast<std::size_t>(rightBase + r)]) {
            result.vertexCoverRight.push_back(r);
        }
    }
    return result;
}

inline int edgeDisjointPaths(int n, const std::vector<std::pair<int, int>>& edges, int s, int t,
                              bool directed) {
    MaxFlow flow(n);
    for (const auto& [u, v] : edges) {
        flow.addEdge(u, v, 1);
        if (!directed) {
            flow.addEdge(v, u, 1);
        }
    }
    return static_cast<int>(flow.maxflow(s, t));
}

}
