#include "bfs_dfs.hpp"

#include <algorithm>
#include <queue>

namespace {

std::vector<int> reconstruct(const std::vector<int>& parent, int start, int target) {
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

}

std::vector<int> bfsOrder(const Graph& g, int start) {
    std::vector<bool> visited(g.numVertices(), false);
    std::vector<int> order;
    std::queue<int> q;
    q.push(start);
    visited[start] = true;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        order.push_back(u);
        for (const auto& e : g.neighbors(u)) {
            if (!visited[e.to]) {
                visited[e.to] = true;
                q.push(e.to);
            }
        }
    }
    return order;
}

std::vector<int> bfsPath(const Graph& g, int start, int target) {
    std::vector<int> parent(g.numVertices(), -2);
    std::vector<bool> visited(g.numVertices(), false);
    std::queue<int> q;
    q.push(start);
    visited[start] = true;
    parent[start] = -1;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        if (u == target) break;
        for (const auto& e : g.neighbors(u)) {
            if (!visited[e.to]) {
                visited[e.to] = true;
                parent[e.to] = u;
                q.push(e.to);
            }
        }
    }
    return reconstruct(parent, start, target);
}

std::vector<int> dfsOrder(const Graph& g, int start) {
    std::vector<bool> visited(g.numVertices(), false);
    std::vector<int> order;
    std::vector<int> stack;
    stack.push_back(start);
    while (!stack.empty()) {
        int u = stack.back();
        stack.pop_back();
        if (visited[u]) continue;
        visited[u] = true;
        order.push_back(u);
        const auto& neighbors = g.neighbors(u);
        for (auto it = neighbors.rbegin(); it != neighbors.rend(); ++it) {
            if (!visited[it->to]) stack.push_back(it->to);
        }
    }
    return order;
}

std::vector<int> dfsPath(const Graph& g, int start, int target) {
    std::vector<int> parent(g.numVertices(), -2);
    std::vector<bool> visited(g.numVertices(), false);
    std::vector<int> stack;
    stack.push_back(start);
    parent[start] = -1;
    while (!stack.empty()) {
        int u = stack.back();
        stack.pop_back();
        if (visited[u]) continue;
        visited[u] = true;
        if (u == target) break;
        const auto& neighbors = g.neighbors(u);
        for (auto it = neighbors.rbegin(); it != neighbors.rend(); ++it) {
            if (!visited[it->to]) {
                if (parent[it->to] == -2) parent[it->to] = u;
                stack.push_back(it->to);
            }
        }
    }
    return reconstruct(parent, start, target);
}
