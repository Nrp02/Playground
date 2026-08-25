#include <algorithm>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "../src/disjoint_set_union.hpp"

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
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

long long bruteForceMstWeight(std::size_t vertexCount, const std::vector<dsu::Edge>& edges) {
    std::size_t edgeCount = edges.size();
    if (vertexCount == 0) {
        return 0;
    }
    std::size_t needed = vertexCount - 1;
    std::vector<std::size_t> indices(edgeCount);
    std::iota(indices.begin(), indices.end(), 0);

    bool found = false;
    long long best = 0;

    std::vector<bool> mask(edgeCount, false);
    std::fill(mask.begin(), mask.begin() + static_cast<long>(needed), true);
    std::sort(mask.begin(), mask.end());

    do {
        dsu::DisjointSetUnion components(vertexCount);
        long long total = 0;
        std::size_t unions = 0;
        for (std::size_t i = 0; i < edgeCount; ++i) {
            if (mask[i]) {
                if (components.unite(edges[i].u, edges[i].v)) {
                    ++unions;
                }
                total += edges[i].weight;
            }
        }
        if (unions == needed && components.setCount() == 1) {
            if (!found || total < best) {
                found = true;
                best = total;
            }
        }
    } while (std::next_permutation(mask.begin(), mask.end()));

    return best;
}

}

int main() {
    {
        dsu::DisjointSetUnion set(5);
        expectEq<std::size_t>(set.setCount(), 5, "new DSU has n singleton sets");
        for (std::size_t i = 0; i < 5; ++i) {
            expectEq<std::size_t>(set.find(i), i, "each element starts as its own root");
        }
    }

    {
        dsu::DisjointSetUnion set(6);
        expectTrue(set.unite(0, 1), "first union succeeds");
        expectTrue(!set.unite(0, 1), "repeated union returns false");
        expectTrue(set.connected(0, 1), "united elements are connected");
        expectTrue(!set.connected(0, 2), "unrelated elements are not connected");
        expectEq<std::size_t>(set.setCount(), 5, "set count decreases by one after a union");
    }

    {
        dsu::DisjointSetUnion set(6);
        set.unite(0, 1);
        set.unite(1, 2);
        set.unite(3, 4);
        expectTrue(set.connected(0, 2), "transitive union connects 0 and 2");
        expectEq<std::size_t>(set.sizeOfSet(0), 3, "sizeOfSet reflects merged component size");
        expectEq<std::size_t>(set.sizeOfSet(5), 1, "untouched element keeps size one");
        set.unite(2, 4);
        expectTrue(set.connected(0, 3), "chained unions connect distant components");
        expectEq<std::size_t>(set.sizeOfSet(0), 5, "sizeOfSet grows after merging components");
        expectEq<std::size_t>(set.setCount(), 2, "set count matches remaining components");
    }

    {
        const std::size_t n = 2000;
        dsu::DisjointSetUnion set(n);
        for (std::size_t i = 1; i < n; ++i) {
            set.unite(i - 1, i);
        }
        std::size_t root = set.find(0);
        bool allSameRoot = true;
        for (std::size_t i = 0; i < n; ++i) {
            if (set.find(i) != root) {
                allSameRoot = false;
                break;
            }
        }
        expectTrue(allSameRoot, "path compression keeps long chain fully connected");
        expectEq<std::size_t>(set.setCount(), 1, "long chain collapses into a single set");
        expectEq<std::size_t>(set.sizeOfSet(0), n, "sizeOfSet counts all elements in the chain");
    }

    {
        std::mt19937 rng(123);
        const std::size_t n = 500;
        std::uniform_int_distribution<std::size_t> dist(0, n - 1);
        dsu::DisjointSetUnion set(n);
        std::vector<std::size_t> parentBefore(n);
        for (std::size_t i = 0; i < n; ++i) {
            parentBefore[i] = i;
        }
        for (int i = 0; i < 3000; ++i) {
            std::size_t a = dist(rng);
            std::size_t b = dist(rng);
            set.unite(a, b);
        }
        bool consistent = true;
        std::size_t sample = set.find(0);
        for (std::size_t i = 1; i < n; ++i) {
            if (set.connected(0, i) != (set.find(i) == sample)) {
                consistent = false;
                break;
            }
        }
        expectTrue(consistent, "connected() agrees with find() after many random unions");
    }

    {
        dsu::DisjointSetUnion set(3);
        bool threw = false;
        try {
            set.find(10);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        expectTrue(threw, "find throws out_of_range for invalid index");
    }

    {
        std::vector<dsu::Edge> edges = {
            {0, 1, 4}, {0, 2, 4}, {1, 2, 2}, {1, 3, 5},
            {2, 3, 5}, {2, 4, 11}, {3, 4, 6}, {3, 5, 1},
            {4, 5, 7}
        };
        dsu::MstResult mst = dsu::kruskalMst(6, edges);
        expectTrue(mst.spansAllVertices, "MST spans all vertices of a connected graph");
        expectEq<std::size_t>(mst.edges.size(), 5, "MST of 6 vertices uses exactly 5 edges");
        long long expected = bruteForceMstWeight(6, edges);
        expectEq<long long>(mst.totalWeight, expected, "Kruskal MST weight matches brute-force reference");
    }

    {
        std::mt19937 rng(999);
        std::uniform_int_distribution<long long> weightDist(1, 20);
        const std::size_t n = 6;
        std::vector<dsu::Edge> edges;
        for (std::size_t u = 0; u < n; ++u) {
            for (std::size_t v = u + 1; v < n; ++v) {
                if (weightDist(rng) <= 15) {
                    edges.push_back({u, v, weightDist(rng)});
                }
            }
        }
        for (std::size_t i = 1; i < n; ++i) {
            edges.push_back({i - 1, i, weightDist(rng)});
        }
        dsu::MstResult mst = dsu::kruskalMst(n, edges);
        expectTrue(mst.spansAllVertices, "randomly generated connected graph MST spans all vertices");
        long long expected = bruteForceMstWeight(n, edges);
        expectEq<long long>(mst.totalWeight, expected, "randomized MST weight matches brute-force reference");
    }

    {
        std::vector<dsu::Edge> edges = {{0, 1, 1}, {2, 3, 1}};
        dsu::MstResult mst = dsu::kruskalMst(4, edges);
        expectTrue(!mst.spansAllVertices, "disconnected graph does not span all vertices");
        expectEq<std::size_t>(mst.edges.size(), 2, "disconnected graph MST forest uses available acyclic edges");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
