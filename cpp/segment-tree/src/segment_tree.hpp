#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace segtree {

struct SumPolicy {
    using Value = long long;
    using Lazy = long long;

    static Value identity() { return 0; }
    static Lazy noLazy() { return 0; }

    static Value combine(const Value& a, const Value& b) { return a + b; }

    static Value applyLazy(const Value& value, const Lazy& delta, std::size_t rangeLen) {
        return value + delta * static_cast<Value>(rangeLen);
    }

    static Lazy composeLazy(const Lazy& oldDelta, const Lazy& newDelta) {
        return oldDelta + newDelta;
    }
};

struct MinPolicy {
    using Value = long long;
    using Lazy = long long;

    static Value identity() { return std::numeric_limits<Value>::max(); }
    static Lazy noLazy() { return 0; }

    static Value combine(const Value& a, const Value& b) { return std::min(a, b); }

    static Value applyLazy(const Value& value, const Lazy& delta, std::size_t) {
        return value + delta;
    }

    static Lazy composeLazy(const Lazy& oldDelta, const Lazy& newDelta) {
        return oldDelta + newDelta;
    }
};

template <typename Policy>
class SegmentTree {
public:
    using Value = typename Policy::Value;
    using Lazy = typename Policy::Lazy;

    explicit SegmentTree(const std::vector<Value>& data)
        : n_(data.size()),
          tree_(4 * std::max<std::size_t>(n_, 1), Policy::identity()),
          lazy_(4 * std::max<std::size_t>(n_, 1), Policy::noLazy()),
          hasLazy_(4 * std::max<std::size_t>(n_, 1), false) {
        if (n_ > 0) {
            build(1, 0, n_ - 1, data);
        }
    }

    void rangeUpdate(std::size_t left, std::size_t right, const Lazy& delta) {
        if (n_ == 0) {
            return;
        }
        update(1, 0, n_ - 1, left, right, delta);
    }

    Value rangeQuery(std::size_t left, std::size_t right) {
        return query(1, 0, n_ - 1, left, right);
    }

    std::size_t size() const { return n_; }

private:
    std::size_t n_;
    std::vector<Value> tree_;
    std::vector<Lazy> lazy_;
    std::vector<bool> hasLazy_;

    void build(std::size_t node, std::size_t lo, std::size_t hi, const std::vector<Value>& data) {
        if (lo == hi) {
            tree_[node] = data[lo];
            return;
        }
        std::size_t mid = lo + (hi - lo) / 2;
        build(2 * node, lo, mid, data);
        build(2 * node + 1, mid + 1, hi, data);
        tree_[node] = Policy::combine(tree_[2 * node], tree_[2 * node + 1]);
    }

    void applyToNode(std::size_t node, std::size_t lo, std::size_t hi, const Lazy& delta) {
        tree_[node] = Policy::applyLazy(tree_[node], delta, hi - lo + 1);
        lazy_[node] = hasLazy_[node] ? Policy::composeLazy(lazy_[node], delta) : delta;
        hasLazy_[node] = true;
    }

    void pushDown(std::size_t node, std::size_t lo, std::size_t hi) {
        if (!hasLazy_[node]) {
            return;
        }
        std::size_t mid = lo + (hi - lo) / 2;
        applyToNode(2 * node, lo, mid, lazy_[node]);
        applyToNode(2 * node + 1, mid + 1, hi, lazy_[node]);
        lazy_[node] = Policy::noLazy();
        hasLazy_[node] = false;
    }

    void update(std::size_t node, std::size_t lo, std::size_t hi, std::size_t left, std::size_t right,
                const Lazy& delta) {
        if (right < lo || hi < left) {
            return;
        }
        if (left <= lo && hi <= right) {
            applyToNode(node, lo, hi, delta);
            return;
        }
        pushDown(node, lo, hi);
        std::size_t mid = lo + (hi - lo) / 2;
        update(2 * node, lo, mid, left, right, delta);
        update(2 * node + 1, mid + 1, hi, left, right, delta);
        tree_[node] = Policy::combine(tree_[2 * node], tree_[2 * node + 1]);
    }

    Value query(std::size_t node, std::size_t lo, std::size_t hi, std::size_t left, std::size_t right) {
        if (right < lo || hi < left) {
            return Policy::identity();
        }
        if (left <= lo && hi <= right) {
            return tree_[node];
        }
        pushDown(node, lo, hi);
        std::size_t mid = lo + (hi - lo) / 2;
        Value leftResult = query(2 * node, lo, mid, left, right);
        Value rightResult = query(2 * node + 1, mid + 1, hi, left, right);
        return Policy::combine(leftResult, rightResult);
    }
};

using SumSegmentTree = SegmentTree<SumPolicy>;
using MinSegmentTree = SegmentTree<MinPolicy>;

}
