#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace rbt {

enum class Color { RED, BLACK };

template <typename Key, typename Compare = std::less<Key>>
class RedBlackTree {
public:
    RedBlackTree() = default;
    RedBlackTree(const RedBlackTree&) = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;

    ~RedBlackTree() {
        destroy(root_);
    }

    bool insert(const Key& key) {
        Node* parent = nullptr;
        Node* current = root_;
        while (current != nullptr) {
            parent = current;
            if (less_(key, current->key)) {
                current = current->left;
            } else if (less_(current->key, key)) {
                current = current->right;
            } else {
                return false;
            }
        }

        Node* node = new Node(key, parent);
        if (parent == nullptr) {
            root_ = node;
        } else if (less_(key, parent->key)) {
            parent->left = node;
        } else {
            parent->right = node;
        }

        insertFixup(node);
        ++size_;
        return true;
    }

    bool contains(const Key& key) const {
        return findNode(key) != nullptr;
    }

    bool erase(const Key& key) {
        Node* target = findNode(key);
        if (target == nullptr) {
            return false;
        }
        eraseNode(target);
        --size_;
        return true;
    }

    std::size_t size() const {
        return size_;
    }

    bool empty() const {
        return size_ == 0;
    }

    std::vector<Key> inorder() const {
        std::vector<Key> result;
        result.reserve(size_);
        inorderCollect(root_, result);
        return result;
    }

    int blackHeight() const {
        return blackHeightOf(root_);
    }

    bool isValidRedBlackTree() const {
        if (root_ != nullptr && root_->color != Color::BLACK) {
            return false;
        }
        return blackHeightOf(root_) != -1;
    }

private:
    struct Node {
        Key key;
        Color color;
        Node* left = nullptr;
        Node* right = nullptr;
        Node* parent;

        Node(const Key& k, Node* p) : key(k), color(Color::RED), parent(p) {}
    };

    Node* root_ = nullptr;
    std::size_t size_ = 0;
    Compare less_;

    static void destroy(Node* node) {
        if (node == nullptr) {
            return;
        }
        destroy(node->left);
        destroy(node->right);
        delete node;
    }

    Node* findNode(const Key& key) const {
        Node* current = root_;
        while (current != nullptr) {
            if (less_(key, current->key)) {
                current = current->left;
            } else if (less_(current->key, key)) {
                current = current->right;
            } else {
                return current;
            }
        }
        return nullptr;
    }

    void inorderCollect(Node* node, std::vector<Key>& out) const {
        if (node == nullptr) {
            return;
        }
        inorderCollect(node->left, out);
        out.push_back(node->key);
        inorderCollect(node->right, out);
    }

    int blackHeightOf(Node* node) const {
        if (node == nullptr) {
            return 1;
        }
        if (node->color == Color::RED) {
            bool leftRed = node->left != nullptr && node->left->color == Color::RED;
            bool rightRed = node->right != nullptr && node->right->color == Color::RED;
            if (leftRed || rightRed) {
                return -1;
            }
        }
        int leftHeight = blackHeightOf(node->left);
        int rightHeight = blackHeightOf(node->right);
        if (leftHeight == -1 || rightHeight == -1 || leftHeight != rightHeight) {
            return -1;
        }
        return leftHeight + (node->color == Color::BLACK ? 1 : 0);
    }

    void rotateLeft(Node* node) {
        Node* pivot = node->right;
        node->right = pivot->left;
        if (pivot->left != nullptr) {
            pivot->left->parent = node;
        }
        pivot->parent = node->parent;
        if (node->parent == nullptr) {
            root_ = pivot;
        } else if (node == node->parent->left) {
            node->parent->left = pivot;
        } else {
            node->parent->right = pivot;
        }
        pivot->left = node;
        node->parent = pivot;
    }

    void rotateRight(Node* node) {
        Node* pivot = node->left;
        node->left = pivot->right;
        if (pivot->right != nullptr) {
            pivot->right->parent = node;
        }
        pivot->parent = node->parent;
        if (node->parent == nullptr) {
            root_ = pivot;
        } else if (node == node->parent->right) {
            node->parent->right = pivot;
        } else {
            node->parent->left = pivot;
        }
        pivot->right = node;
        node->parent = pivot;
    }

    static Color colorOf(Node* node) {
        return node == nullptr ? Color::BLACK : node->color;
    }

    void insertFixup(Node* node) {
        while (colorOf(node->parent) == Color::RED) {
            Node* parent = node->parent;
            Node* grandparent = parent->parent;
            if (parent == grandparent->left) {
                Node* uncle = grandparent->right;
                if (colorOf(uncle) == Color::RED) {
                    parent->color = Color::BLACK;
                    uncle->color = Color::BLACK;
                    grandparent->color = Color::RED;
                    node = grandparent;
                } else {
                    if (node == parent->right) {
                        node = parent;
                        rotateLeft(node);
                        parent = node->parent;
                    }
                    parent->color = Color::BLACK;
                    grandparent->color = Color::RED;
                    rotateRight(grandparent);
                }
            } else {
                Node* uncle = grandparent->left;
                if (colorOf(uncle) == Color::RED) {
                    parent->color = Color::BLACK;
                    uncle->color = Color::BLACK;
                    grandparent->color = Color::RED;
                    node = grandparent;
                } else {
                    if (node == parent->left) {
                        node = parent;
                        rotateRight(node);
                        parent = node->parent;
                    }
                    parent->color = Color::BLACK;
                    grandparent->color = Color::RED;
                    rotateLeft(grandparent);
                }
            }
        }
        root_->color = Color::BLACK;
    }

    void transplant(Node* target, Node* replacement) {
        if (target->parent == nullptr) {
            root_ = replacement;
        } else if (target == target->parent->left) {
            target->parent->left = replacement;
        } else {
            target->parent->right = replacement;
        }
        if (replacement != nullptr) {
            replacement->parent = target->parent;
        }
    }

    static Node* minimum(Node* node) {
        while (node->left != nullptr) {
            node = node->left;
        }
        return node;
    }

    void eraseNode(Node* target) {
        Node* removedOrMoved = target;
        Color originalColor = removedOrMoved->color;
        Node* fixupNode = nullptr;
        Node* fixupParent = nullptr;

        if (target->left == nullptr) {
            fixupNode = target->right;
            fixupParent = target->parent;
            transplant(target, target->right);
        } else if (target->right == nullptr) {
            fixupNode = target->left;
            fixupParent = target->parent;
            transplant(target, target->left);
        } else {
            removedOrMoved = minimum(target->right);
            originalColor = removedOrMoved->color;
            fixupNode = removedOrMoved->right;

            if (removedOrMoved->parent == target) {
                fixupParent = removedOrMoved;
            } else {
                fixupParent = removedOrMoved->parent;
                transplant(removedOrMoved, removedOrMoved->right);
                removedOrMoved->right = target->right;
                removedOrMoved->right->parent = removedOrMoved;
            }
            transplant(target, removedOrMoved);
            removedOrMoved->left = target->left;
            removedOrMoved->left->parent = removedOrMoved;
            removedOrMoved->color = target->color;
        }

        delete target;

        if (originalColor == Color::BLACK) {
            eraseFixup(fixupNode, fixupParent);
        }
    }

    void eraseFixup(Node* node, Node* parent) {
        while (node != root_ && colorOf(node) == Color::BLACK) {
            if (node == parent->left) {
                Node* sibling = parent->right;
                if (colorOf(sibling) == Color::RED) {
                    sibling->color = Color::BLACK;
                    parent->color = Color::RED;
                    rotateLeft(parent);
                    sibling = parent->right;
                }
                if (colorOf(sibling->left) == Color::BLACK && colorOf(sibling->right) == Color::BLACK) {
                    sibling->color = Color::RED;
                    node = parent;
                    parent = node->parent;
                } else {
                    if (colorOf(sibling->right) == Color::BLACK) {
                        if (sibling->left != nullptr) {
                            sibling->left->color = Color::BLACK;
                        }
                        sibling->color = Color::RED;
                        rotateRight(sibling);
                        sibling = parent->right;
                    }
                    sibling->color = parent->color;
                    parent->color = Color::BLACK;
                    if (sibling->right != nullptr) {
                        sibling->right->color = Color::BLACK;
                    }
                    rotateLeft(parent);
                    node = root_;
                    parent = nullptr;
                }
            } else {
                Node* sibling = parent->left;
                if (colorOf(sibling) == Color::RED) {
                    sibling->color = Color::BLACK;
                    parent->color = Color::RED;
                    rotateRight(parent);
                    sibling = parent->left;
                }
                if (colorOf(sibling->right) == Color::BLACK && colorOf(sibling->left) == Color::BLACK) {
                    sibling->color = Color::RED;
                    node = parent;
                    parent = node->parent;
                } else {
                    if (colorOf(sibling->left) == Color::BLACK) {
                        if (sibling->right != nullptr) {
                            sibling->right->color = Color::BLACK;
                        }
                        sibling->color = Color::RED;
                        rotateLeft(sibling);
                        sibling = parent->left;
                    }
                    sibling->color = parent->color;
                    parent->color = Color::BLACK;
                    if (sibling->left != nullptr) {
                        sibling->left->color = Color::BLACK;
                    }
                    rotateRight(parent);
                    node = root_;
                    parent = nullptr;
                }
            }
        }
        if (node != nullptr) {
            node->color = Color::BLACK;
        }
    }
};

}
