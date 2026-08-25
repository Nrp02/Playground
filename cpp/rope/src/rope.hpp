#ifndef ROPE_ROPE_HPP
#define ROPE_ROPE_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rope {

class Rope {
public:
    Rope() : root_(nullptr) {}

    explicit Rope(const std::string& text) : root_(buildFromString(text)) {}

    std::size_t length() const {
        return lengthOf(root_);
    }

    bool empty() const {
        return length() == 0;
    }

    std::size_t treeHeight() const {
        return heightOf(root_);
    }

    char charAt(std::size_t index) const {
        if (index >= length()) {
            throw std::out_of_range("Rope::charAt index out of range");
        }
        NodePtr node = root_;
        std::size_t idx = index;
        while (true) {
            if (!node->left && !node->right) {
                return node->data[idx];
            }
            std::size_t leftLen = lengthOf(node->left);
            if (idx < leftLen) {
                node = node->left;
            } else {
                idx -= leftLen;
                node = node->right;
            }
        }
    }

    std::string substring(std::size_t start, std::size_t end) const {
        if (start > end || end > length()) {
            throw std::out_of_range("Rope::substring range out of bounds");
        }
        std::string out;
        out.reserve(end - start);
        substringHelper(root_, start, end, out);
        return out;
    }

    void insert(std::size_t index, const std::string& text) {
        if (index > length()) {
            throw std::out_of_range("Rope::insert index out of range");
        }
        if (text.empty()) {
            return;
        }
        auto parts = split(root_, index);
        NodePtr middle = buildFromString(text);
        root_ = concatRaw(concatRaw(parts.first, middle), parts.second);
        rebalanceIfNeeded();
    }

    void erase(std::size_t start, std::size_t count) {
        if (count == 0) {
            return;
        }
        if (start + count > length()) {
            throw std::out_of_range("Rope::erase range out of bounds");
        }
        auto front = split(root_, start);
        auto back = split(front.second, count);
        root_ = concatRaw(front.first, back.second);
        rebalanceIfNeeded();
    }

    void append(const std::string& text) {
        insert(length(), text);
    }

    std::string toString() const {
        std::string out;
        out.reserve(length());
        toStringHelper(root_, out);
        return out;
    }

    static Rope concat(const Rope& a, const Rope& b) {
        Rope result(concatRaw(a.root_, b.root_));
        result.rebalanceIfNeeded();
        return result;
    }

private:
    struct Node {
        std::string data;
        std::shared_ptr<Node> left;
        std::shared_ptr<Node> right;
        std::size_t len = 0;
        std::size_t height = 1;
    };
    using NodePtr = std::shared_ptr<Node>;

    static constexpr std::size_t kLeafMaxSize = 16;
    static constexpr std::size_t kRebalanceSlack = 4;

    NodePtr root_;

    explicit Rope(NodePtr root) : root_(std::move(root)) {}

    static std::size_t lengthOf(const NodePtr& node) {
        return node ? node->len : 0;
    }

    static std::size_t heightOf(const NodePtr& node) {
        return node ? node->height : 0;
    }

    static NodePtr makeLeaf(const std::string& s) {
        NodePtr n = std::make_shared<Node>();
        n->data = s;
        n->len = s.size();
        n->height = 1;
        return n;
    }

    static NodePtr concatRaw(NodePtr a, NodePtr b) {
        if (!a) {
            return b;
        }
        if (!b) {
            return a;
        }
        NodePtr n = std::make_shared<Node>();
        n->left = std::move(a);
        n->right = std::move(b);
        n->len = n->left->len + n->right->len;
        n->height = 1 + std::max(n->left->height, n->right->height);
        return n;
    }

    static NodePtr buildBalanced(std::vector<NodePtr>& leaves, std::size_t lo, std::size_t hi) {
        if (lo == hi) {
            return leaves[lo];
        }
        std::size_t mid = lo + (hi - lo) / 2;
        NodePtr left = buildBalanced(leaves, lo, mid);
        NodePtr right = buildBalanced(leaves, mid + 1, hi);
        return concatRaw(left, right);
    }

    static NodePtr buildFromString(const std::string& s) {
        if (s.empty()) {
            return nullptr;
        }
        std::vector<NodePtr> leaves;
        for (std::size_t i = 0; i < s.size(); i += kLeafMaxSize) {
            std::size_t chunkLen = std::min(kLeafMaxSize, s.size() - i);
            leaves.push_back(makeLeaf(s.substr(i, chunkLen)));
        }
        return buildBalanced(leaves, 0, leaves.size() - 1);
    }

    static void collectLeaves(const NodePtr& node, std::vector<NodePtr>& out) {
        if (!node) {
            return;
        }
        if (!node->left && !node->right) {
            out.push_back(node);
            return;
        }
        collectLeaves(node->left, out);
        collectLeaves(node->right, out);
    }

    static std::pair<NodePtr, NodePtr> split(const NodePtr& node, std::size_t index) {
        if (!node) {
            return {nullptr, nullptr};
        }
        if (!node->left && !node->right) {
            if (index == 0) {
                return {nullptr, node};
            }
            if (index >= node->len) {
                return {node, nullptr};
            }
            NodePtr left = makeLeaf(node->data.substr(0, index));
            NodePtr right = makeLeaf(node->data.substr(index));
            return {left, right};
        }
        std::size_t leftLen = lengthOf(node->left);
        if (index <= leftLen) {
            auto parts = split(node->left, index);
            return {parts.first, concatRaw(parts.second, node->right)};
        }
        auto parts = split(node->right, index - leftLen);
        return {concatRaw(node->left, parts.first), parts.second};
    }

    static void substringHelper(const NodePtr& node, std::size_t lo, std::size_t hi, std::string& out) {
        if (!node || lo >= hi) {
            return;
        }
        if (!node->left && !node->right) {
            out += node->data.substr(lo, hi - lo);
            return;
        }
        std::size_t leftLen = lengthOf(node->left);
        if (lo < leftLen) {
            substringHelper(node->left, lo, std::min(hi, leftLen), out);
        }
        if (hi > leftLen) {
            std::size_t rightLo = lo > leftLen ? lo - leftLen : 0;
            substringHelper(node->right, rightLo, hi - leftLen, out);
        }
    }

    static void toStringHelper(const NodePtr& node, std::string& out) {
        if (!node) {
            return;
        }
        if (!node->left && !node->right) {
            out += node->data;
            return;
        }
        toStringHelper(node->left, out);
        toStringHelper(node->right, out);
    }

    void rebalanceIfNeeded() {
        std::size_t n = length();
        if (n == 0) {
            return;
        }
        double logN = std::log2(static_cast<double>(n + 1));
        std::size_t threshold = static_cast<std::size_t>(2.0 * logN) + kRebalanceSlack;
        if (heightOf(root_) > threshold) {
            std::vector<NodePtr> leaves;
            collectLeaves(root_, leaves);
            root_ = buildBalanced(leaves, 0, leaves.size() - 1);
        }
    }
};

}

#endif
