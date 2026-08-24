#pragma once

#include <cstddef>
#include <list>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace lru {

template <typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("LRUCache capacity must be > 0");
        }
    }

    bool get(const Key& key, Value& outValue) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            ++misses_;
            return false;
        }
        ++hits_;
        touch(it->second);
        outValue = it->second->second;
        return true;
    }

    Value getOr(const Key& key, const Value& fallback) {
        Value value;
        if (get(key, value)) {
            return value;
        }
        return fallback;
    }

    void put(const Key& key, const Value& value) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = value;
            touch(it->second);
            return;
        }

        entries_.emplace_front(key, value);
        index_[key] = entries_.begin();

        if (entries_.size() > capacity_) {
            evictLeastRecentlyUsed();
        }
    }

    bool erase(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return false;
        }
        entries_.erase(it->second);
        index_.erase(it);
        return true;
    }

    bool contains(const Key& key) const {
        return index_.find(key) != index_.end();
    }

    std::size_t size() const {
        return entries_.size();
    }

    std::size_t capacity() const {
        return capacity_;
    }

    void clear() {
        entries_.clear();
        index_.clear();
    }

    std::size_t hits() const {
        return hits_;
    }

    std::size_t misses() const {
        return misses_;
    }

    double hitRate() const {
        const std::size_t total = hits_ + misses_;
        if (total == 0) {
            return 0.0;
        }
        return static_cast<double>(hits_) / static_cast<double>(total);
    }

    std::list<std::pair<Key, Value>> entriesMostToLeastRecent() const {
        return entries_;
    }

private:
    using EntryList = std::list<std::pair<Key, Value>>;
    using EntryIterator = typename EntryList::iterator;

    void touch(EntryIterator it) {
        entries_.splice(entries_.begin(), entries_, it);
    }

    void evictLeastRecentlyUsed() {
        const Key& lruKey = entries_.back().first;
        index_.erase(lruKey);
        entries_.pop_back();
    }

    std::size_t capacity_;
    EntryList entries_;
    std::unordered_map<Key, EntryIterator> index_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
};

}
