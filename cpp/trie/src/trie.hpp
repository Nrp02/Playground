#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace trie {

class Trie {
public:
    Trie() : root_(new Node()) {}
    Trie(const Trie&) = delete;
    Trie& operator=(const Trie&) = delete;

    ~Trie() {
        destroy(root_);
    }

    void insert(const std::string& word) {
        Node* current = root_;
        for (char c : word) {
            int index = charIndex(c);
            if (index < 0) {
                throw std::invalid_argument("Trie: only lowercase a-z characters are supported");
            }
            if (current->children[index] == nullptr) {
                current->children[index] = new Node();
            }
            current = current->children[index];
        }
        if (!current->isWord) {
            current->isWord = true;
            ++wordCount_;
        }
    }

    bool search(const std::string& word) const {
        Node* node = findNode(word);
        return node != nullptr && node->isWord;
    }

    bool startsWith(const std::string& prefix) const {
        return findNode(prefix) != nullptr;
    }

    bool erase(const std::string& word) {
        return eraseHelper(root_, word, 0);
    }

    std::vector<std::string> autocomplete(const std::string& prefix, std::size_t k) const {
        std::vector<std::string> results;
        Node* node = findNode(prefix);
        if (node == nullptr || k == 0) {
            return results;
        }
        std::string current = prefix;
        collect(node, current, k, results);
        return results;
    }

    std::size_t wordCount() const {
        return wordCount_;
    }

    bool empty() const {
        return wordCount_ == 0;
    }

private:
    static constexpr int kAlphabetSize = 26;

    struct Node {
        std::array<Node*, kAlphabetSize> children{};
        bool isWord = false;
    };

    Node* root_;
    std::size_t wordCount_ = 0;

    static int charIndex(char c) {
        if (c >= 'a' && c <= 'z') {
            return c - 'a';
        }
        return -1;
    }

    static void destroy(Node* node) {
        if (node == nullptr) {
            return;
        }
        for (Node* child : node->children) {
            destroy(child);
        }
        delete node;
    }

    Node* findNode(const std::string& word) const {
        Node* current = root_;
        for (char c : word) {
            int index = charIndex(c);
            if (index < 0 || current->children[index] == nullptr) {
                return nullptr;
            }
            current = current->children[index];
        }
        return current;
    }

    bool eraseHelper(Node* node, const std::string& word, std::size_t depth) {
        if (depth == word.size()) {
            if (!node->isWord) {
                return false;
            }
            node->isWord = false;
            --wordCount_;
            return true;
        }
        int index = charIndex(word[depth]);
        if (index < 0) {
            return false;
        }
        Node* child = node->children[index];
        if (child == nullptr) {
            return false;
        }
        bool erased = eraseHelper(child, word, depth + 1);
        if (erased && !child->isWord && isLeaf(child)) {
            node->children[index] = nullptr;
            delete child;
        }
        return erased;
    }

    static bool isLeaf(Node* node) {
        for (Node* child : node->children) {
            if (child != nullptr) {
                return false;
            }
        }
        return true;
    }

    void collect(Node* node, std::string& current, std::size_t k, std::vector<std::string>& results) const {
        if (results.size() >= k) {
            return;
        }
        if (node->isWord) {
            results.push_back(current);
            if (results.size() >= k) {
                return;
            }
        }
        for (int i = 0; i < kAlphabetSize; ++i) {
            if (node->children[i] != nullptr) {
                current.push_back(static_cast<char>('a' + i));
                collect(node->children[i], current, k, results);
                current.pop_back();
                if (results.size() >= k) {
                    return;
                }
            }
        }
    }
};

}
