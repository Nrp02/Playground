#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace itree {

template <typename Payload>
struct Interval {
    long long low;
    long long high;
    std::uint64_t id;
    Payload payload;
};

template <typename Payload>
struct Booking {
    long long low;
    long long high;
    Payload payload;
};

template <typename Payload>
class IntervalTree {
public:
    IntervalTree() = default;
    IntervalTree(const IntervalTree&) = delete;
    IntervalTree& operator=(const IntervalTree&) = delete;

    ~IntervalTree() {
        destroy(root_);
    }

    std::uint64_t insert(long long low, long long high, const Payload& payload) {
        if (high < low) {
            throw std::invalid_argument("interval high must be >= low");
        }
        std::uint64_t id = nextId_++;
        root_ = insertNode(root_, low, high, id, payload);
        locations_.emplace(id, std::make_pair(low, high));
        ++size_;
        return id;
    }

    bool remove(std::uint64_t id) {
        auto it = locations_.find(id);
        if (it == locations_.end()) {
            return false;
        }
        long long low = it->second.first;
        long long high = it->second.second;
        bool removed = false;
        root_ = removeNode(root_, low, high, id, removed);
        if (removed) {
            locations_.erase(it);
            --size_;
        }
        return removed;
    }

    std::size_t size() const {
        return size_;
    }

    bool empty() const {
        return size_ == 0;
    }

    std::vector<Interval<Payload>> stab(long long point, std::size_t& visited) const {
        return overlapping(point, point, visited);
    }

    std::vector<Interval<Payload>> overlapping(long long qlow, long long qhigh, std::size_t& visited) const {
        std::vector<Interval<Payload>> result;
        visited = 0;
        collect(root_, qlow, qhigh, result, visited);
        return result;
    }

    bool anyOverlap(long long qlow, long long qhigh, std::size_t& visited) const {
        visited = 0;
        return anyOverlapImpl(root_, qlow, qhigh, visited);
    }

    std::size_t countOverlapping(long long qlow, long long qhigh, std::size_t& visited) const {
        std::size_t count = 0;
        visited = 0;
        countImpl(root_, qlow, qhigh, count, visited);
        return count;
    }

    bool isValid() const {
        long long maxEndOut = 0;
        int heightOut = 0;
        if (!validate(root_, maxEndOut, heightOut)) {
            return false;
        }
        auto keys = inorderKeys();
        for (std::size_t i = 1; i < keys.size(); ++i) {
            if (!(keys[i - 1] < keys[i])) {
                return false;
            }
        }
        return true;
    }

    int height() const {
        return heightOf(root_);
    }

    std::vector<std::tuple<long long, long long, std::uint64_t>> inorderKeys() const {
        std::vector<std::tuple<long long, long long, std::uint64_t>> out;
        out.reserve(size_);
        collectKeys(root_, out);
        return out;
    }

private:
    struct Node {
        long long low;
        long long high;
        long long maxEnd;
        std::uint64_t id;
        Payload payload;
        int height;
        Node* left;
        Node* right;

        Node(long long l, long long h, std::uint64_t i, const Payload& p)
            : low(l), high(h), maxEnd(h), id(i), payload(p), height(1), left(nullptr), right(nullptr) {}
    };

    Node* root_ = nullptr;
    std::size_t size_ = 0;
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, std::pair<long long, long long>> locations_;

    static void destroy(Node* node) {
        if (node == nullptr) {
            return;
        }
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

    static int heightOf(Node* node) {
        return node == nullptr ? 0 : node->height;
    }

    static long long maxEndOf(Node* node) {
        return node == nullptr ? std::numeric_limits<long long>::min() : node->maxEnd;
    }

    static int balanceOf(Node* node) {
        return node == nullptr ? 0 : heightOf(node->left) - heightOf(node->right);
    }

    static void update(Node* node) {
        node->height = 1 + std::max(heightOf(node->left), heightOf(node->right));
        long long m = node->high;
        m = std::max(m, maxEndOf(node->left));
        m = std::max(m, maxEndOf(node->right));
        node->maxEnd = m;
    }

    static Node* rotateLeft(Node* node) {
        Node* pivot = node->right;
        node->right = pivot->left;
        pivot->left = node;
        update(node);
        update(pivot);
        return pivot;
    }

    static Node* rotateRight(Node* node) {
        Node* pivot = node->left;
        node->left = pivot->right;
        pivot->right = node;
        update(node);
        update(pivot);
        return pivot;
    }

    static Node* rebalance(Node* node) {
        update(node);
        int balance = balanceOf(node);
        if (balance > 1) {
            if (balanceOf(node->left) < 0) {
                node->left = rotateLeft(node->left);
            }
            return rotateRight(node);
        }
        if (balance < -1) {
            if (balanceOf(node->right) > 0) {
                node->right = rotateRight(node->right);
            }
            return rotateLeft(node);
        }
        return node;
    }

    static int compareKey(long long low, long long high, std::uint64_t id, Node* node) {
        if (low != node->low) {
            return low < node->low ? -1 : 1;
        }
        if (high != node->high) {
            return high < node->high ? -1 : 1;
        }
        if (id != node->id) {
            return id < node->id ? -1 : 1;
        }
        return 0;
    }

    Node* insertNode(Node* node, long long low, long long high, std::uint64_t id, const Payload& payload) {
        if (node == nullptr) {
            return new Node(low, high, id, payload);
        }
        int cmp = compareKey(low, high, id, node);
        if (cmp < 0) {
            node->left = insertNode(node->left, low, high, id, payload);
        } else {
            node->right = insertNode(node->right, low, high, id, payload);
        }
        return rebalance(node);
    }

    static Node* minNode(Node* node) {
        while (node->left != nullptr) {
            node = node->left;
        }
        return node;
    }

    Node* removeNode(Node* node, long long low, long long high, std::uint64_t id, bool& removed) {
        if (node == nullptr) {
            removed = false;
            return nullptr;
        }
        int cmp = compareKey(low, high, id, node);
        if (cmp < 0) {
            node->left = removeNode(node->left, low, high, id, removed);
        } else if (cmp > 0) {
            node->right = removeNode(node->right, low, high, id, removed);
        } else {
            removed = true;
            if (node->left == nullptr || node->right == nullptr) {
                Node* child = node->left != nullptr ? node->left : node->right;
                delete node;
                return child;
            }
            Node* successor = minNode(node->right);
            node->low = successor->low;
            node->high = successor->high;
            node->id = successor->id;
            node->payload = successor->payload;
            bool dummy = false;
            node->right = removeNode(node->right, successor->low, successor->high, successor->id, dummy);
        }
        return rebalance(node);
    }

    static bool overlaps(long long low, long long high, long long qlow, long long qhigh) {
        return low <= qhigh && qlow <= high;
    }

    static void collect(Node* node, long long qlow, long long qhigh, std::vector<Interval<Payload>>& out, std::size_t& visited) {
        if (node == nullptr) {
            return;
        }
        ++visited;
        if (overlaps(node->low, node->high, qlow, qhigh)) {
            out.push_back(Interval<Payload>{node->low, node->high, node->id, node->payload});
        }
        if (node->left != nullptr && node->left->maxEnd >= qlow) {
            collect(node->left, qlow, qhigh, out, visited);
        }
        if (node->right != nullptr && node->low <= qhigh && node->right->maxEnd >= qlow) {
            collect(node->right, qlow, qhigh, out, visited);
        }
    }

    static bool anyOverlapImpl(Node* node, long long qlow, long long qhigh, std::size_t& visited) {
        if (node == nullptr) {
            return false;
        }
        ++visited;
        if (overlaps(node->low, node->high, qlow, qhigh)) {
            return true;
        }
        if (node->left != nullptr && node->left->maxEnd >= qlow) {
            if (anyOverlapImpl(node->left, qlow, qhigh, visited)) {
                return true;
            }
        }
        if (node->right != nullptr && node->low <= qhigh && node->right->maxEnd >= qlow) {
            if (anyOverlapImpl(node->right, qlow, qhigh, visited)) {
                return true;
            }
        }
        return false;
    }

    static void countImpl(Node* node, long long qlow, long long qhigh, std::size_t& count, std::size_t& visited) {
        if (node == nullptr) {
            return;
        }
        ++visited;
        if (overlaps(node->low, node->high, qlow, qhigh)) {
            ++count;
        }
        if (node->left != nullptr && node->left->maxEnd >= qlow) {
            countImpl(node->left, qlow, qhigh, count, visited);
        }
        if (node->right != nullptr && node->low <= qhigh && node->right->maxEnd >= qlow) {
            countImpl(node->right, qlow, qhigh, count, visited);
        }
    }

    static void collectKeys(Node* node, std::vector<std::tuple<long long, long long, std::uint64_t>>& out) {
        if (node == nullptr) {
            return;
        }
        collectKeys(node->left, out);
        out.emplace_back(node->low, node->high, node->id);
        collectKeys(node->right, out);
    }

    static bool validate(Node* node, long long& maxEndOut, int& heightOut) {
        if (node == nullptr) {
            maxEndOut = std::numeric_limits<long long>::min();
            heightOut = 0;
            return true;
        }
        long long leftMax = std::numeric_limits<long long>::min();
        long long rightMax = std::numeric_limits<long long>::min();
        int leftHeight = 0;
        int rightHeight = 0;
        if (!validate(node->left, leftMax, leftHeight)) {
            return false;
        }
        if (!validate(node->right, rightMax, rightHeight)) {
            return false;
        }
        int balance = leftHeight - rightHeight;
        if (balance > 1 || balance < -1) {
            return false;
        }
        int expectedHeight = 1 + std::max(leftHeight, rightHeight);
        if (node->height != expectedHeight) {
            return false;
        }
        heightOut = expectedHeight;
        long long expectedMax = node->high;
        if (node->left != nullptr) {
            expectedMax = std::max(expectedMax, leftMax);
        }
        if (node->right != nullptr) {
            expectedMax = std::max(expectedMax, rightMax);
        }
        if (node->maxEnd != expectedMax) {
            return false;
        }
        maxEndOut = expectedMax;
        return true;
    }
};

template <typename Payload>
std::vector<Booking<Payload>> maxNonOverlapping(std::vector<Booking<Payload>> bookings) {
    std::sort(bookings.begin(), bookings.end(), [](const Booking<Payload>& a, const Booking<Payload>& b) {
        return a.high < b.high;
    });
    std::vector<Booking<Payload>> selected;
    long long lastEnd = std::numeric_limits<long long>::min();
    bool first = true;
    for (const auto& booking : bookings) {
        if (first || booking.low > lastEnd) {
            selected.push_back(booking);
            lastEnd = booking.high;
            first = false;
        }
    }
    return selected;
}

template <typename Payload>
std::vector<std::pair<Booking<Payload>, Booking<Payload>>> findConflicts(const std::vector<Booking<Payload>>& bookings) {
    IntervalTree<Payload> tree;
    std::unordered_map<std::uint64_t, std::size_t> idToIndex;
    std::vector<std::pair<Booking<Payload>, Booking<Payload>>> conflicts;
    for (std::size_t i = 0; i < bookings.size(); ++i) {
        const auto& booking = bookings[i];
        std::size_t visited = 0;
        auto matches = tree.overlapping(booking.low, booking.high, visited);
        for (const auto& match : matches) {
            std::size_t j = idToIndex.at(match.id);
            conflicts.emplace_back(bookings[j], booking);
        }
        std::uint64_t id = tree.insert(booking.low, booking.high, booking.payload);
        idToIndex.emplace(id, i);
    }
    return conflicts;
}

}
