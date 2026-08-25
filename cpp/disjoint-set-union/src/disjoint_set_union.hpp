#ifndef DSU_DISJOINT_SET_UNION_HPP
#define DSU_DISJOINT_SET_UNION_HPP

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace dsu {

class DisjointSetUnion {
public:
    explicit DisjointSetUnion(std::size_t n)
        : parent_(n), rank_(n, 0), size_(n, 1), setCount_(n) {
        std::iota(parent_.begin(), parent_.end(), 0);
    }

    std::size_t find(std::size_t x) {
        checkIndex(x);
        return findImpl(x);
    }

    bool unite(std::size_t a, std::size_t b) {
        checkIndex(a);
        checkIndex(b);
        std::size_t rootA = findImpl(a);
        std::size_t rootB = findImpl(b);
        if (rootA == rootB) {
            return false;
        }
        if (rank_[rootA] < rank_[rootB]) {
            std::swap(rootA, rootB);
        }
        parent_[rootB] = rootA;
        size_[rootA] += size_[rootB];
        if (rank_[rootA] == rank_[rootB]) {
            ++rank_[rootA];
        }
        --setCount_;
        return true;
    }

    bool connected(std::size_t a, std::size_t b) {
        return find(a) == find(b);
    }

    std::size_t sizeOfSet(std::size_t x) {
        return size_[findImpl(checkIndex(x))];
    }

    std::size_t setCount() const {
        return setCount_;
    }

    std::size_t elementCount() const {
        return parent_.size();
    }

private:
    std::size_t findImpl(std::size_t x) {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]];
            x = parent_[x];
        }
        return x;
    }

    std::size_t checkIndex(std::size_t x) const {
        if (x >= parent_.size()) {
            throw std::out_of_range("index out of range in DisjointSetUnion");
        }
        return x;
    }

    std::vector<std::size_t> parent_;
    std::vector<std::size_t> rank_;
    std::vector<std::size_t> size_;
    std::size_t setCount_;
};

struct Edge {
    std::size_t u;
    std::size_t v;
    long long weight;
};

struct MstResult {
    std::vector<Edge> edges;
    long long totalWeight;
    bool spansAllVertices;
};

inline MstResult kruskalMst(std::size_t vertexCount, std::vector<Edge> edges) {
    std::sort(edges.begin(), edges.end(), [](const Edge& lhs, const Edge& rhs) {
        return lhs.weight < rhs.weight;
    });

    DisjointSetUnion components(vertexCount);
    MstResult result{{}, 0, false};

    for (const Edge& edge : edges) {
        if (components.unite(edge.u, edge.v)) {
            result.edges.push_back(edge);
            result.totalWeight += edge.weight;
        }
        if (components.setCount() == 1) {
            break;
        }
    }

    result.spansAllVertices = (vertexCount == 0) || (components.setCount() == 1);
    return result;
}

}

#endif
