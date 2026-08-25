#pragma once

#include <cstddef>
#include <random>
#include <vector>

namespace sl {

template <typename T>
class SkipList {
public:
    explicit SkipList(double p = 0.5, int maxLevel = 16, unsigned seed = std::random_device{}())
        : p_(p), maxLevel_(maxLevel), level_(1), size_(0), rng_(seed), dist_(0.0, 1.0) {
        head_ = new Node(T(), maxLevel_);
    }

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;

    SkipList(SkipList&& other) noexcept
        : p_(other.p_), maxLevel_(other.maxLevel_), level_(other.level_), size_(other.size_),
          rng_(std::move(other.rng_)), dist_(other.dist_), head_(other.head_) {
        other.head_ = new Node(T(), other.maxLevel_);
        other.level_ = 1;
        other.size_ = 0;
    }

    SkipList& operator=(SkipList&& other) noexcept {
        if (this != &other) {
            clear();
            delete head_;
            p_ = other.p_;
            maxLevel_ = other.maxLevel_;
            level_ = other.level_;
            size_ = other.size_;
            rng_ = std::move(other.rng_);
            dist_ = other.dist_;
            head_ = other.head_;
            other.head_ = new Node(T(), other.maxLevel_);
            other.level_ = 1;
            other.size_ = 0;
        }
        return *this;
    }

    ~SkipList() {
        clear();
        delete head_;
    }

    bool insert(const T& value) {
        std::vector<Node*> update(maxLevel_, nullptr);
        Node* current = head_;
        for (int i = level_ - 1; i >= 0; --i) {
            while (current->forward[i] != nullptr && current->forward[i]->value < value) {
                current = current->forward[i];
            }
            update[i] = current;
        }
        current = current->forward[0];

        if (current != nullptr && current->value == value) {
            return false;
        }

        int newLevel = randomLevel();
        if (newLevel > level_) {
            for (int i = level_; i < newLevel; ++i) {
                update[i] = head_;
            }
            level_ = newLevel;
        }

        Node* created = new Node(value, newLevel);
        for (int i = 0; i < newLevel; ++i) {
            created->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = created;
        }
        ++size_;
        return true;
    }

    bool contains(const T& value) const {
        Node* current = head_;
        for (int i = level_ - 1; i >= 0; --i) {
            while (current->forward[i] != nullptr && current->forward[i]->value < value) {
                current = current->forward[i];
            }
        }
        current = current->forward[0];
        return current != nullptr && current->value == value;
    }

    bool erase(const T& value) {
        std::vector<Node*> update(maxLevel_, nullptr);
        Node* current = head_;
        for (int i = level_ - 1; i >= 0; --i) {
            while (current->forward[i] != nullptr && current->forward[i]->value < value) {
                current = current->forward[i];
            }
            update[i] = current;
        }
        current = current->forward[0];

        if (current == nullptr || !(current->value == value)) {
            return false;
        }

        for (int i = 0; i < level_; ++i) {
            if (update[i]->forward[i] != current) {
                break;
            }
            update[i]->forward[i] = current->forward[i];
        }
        delete current;

        while (level_ > 1 && head_->forward[level_ - 1] == nullptr) {
            --level_;
        }
        --size_;
        return true;
    }

    std::vector<T> range(const T& start, const T& end) const {
        std::vector<T> result;
        if (end < start) {
            return result;
        }
        Node* current = head_;
        for (int i = level_ - 1; i >= 0; --i) {
            while (current->forward[i] != nullptr && current->forward[i]->value < start) {
                current = current->forward[i];
            }
        }
        current = current->forward[0];
        while (current != nullptr && !(end < current->value)) {
            result.push_back(current->value);
            current = current->forward[0];
        }
        return result;
    }

    std::vector<T> toVector() const {
        std::vector<T> result;
        result.reserve(size_);
        Node* current = head_->forward[0];
        while (current != nullptr) {
            result.push_back(current->value);
            current = current->forward[0];
        }
        return result;
    }

    void clear() {
        Node* current = head_->forward[0];
        while (current != nullptr) {
            Node* next = current->forward[0];
            delete current;
            current = next;
        }
        for (int i = 0; i < maxLevel_; ++i) {
            head_->forward[i] = nullptr;
        }
        level_ = 1;
        size_ = 0;
    }

    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }
    int currentLevel() const { return level_; }

private:
    struct Node {
        T value;
        std::vector<Node*> forward;
        Node(T val, int levelCount) : value(std::move(val)), forward(levelCount, nullptr) {}
    };

    int randomLevel() {
        int newLevel = 1;
        while (dist_(rng_) < p_ && newLevel < maxLevel_) {
            ++newLevel;
        }
        return newLevel;
    }

    double p_;
    int maxLevel_;
    int level_;
    std::size_t size_;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_;
    Node* head_;
};

}
