#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace shm {

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class ShardedHashMap {
public:
    explicit ShardedHashMap(std::size_t shardCount = 16)
        : shards_(shardCount == 0 ? 1 : shardCount) {}

    void put(const Key& key, const Value& value) {
        Shard& shard = shardFor(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.table[key] = value;
    }

    bool get(const Key& key, Value& outValue) const {
        const Shard& shard = shardFor(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto it = shard.table.find(key);
        if (it == shard.table.end()) {
            return false;
        }
        outValue = it->second;
        return true;
    }

    std::optional<Value> get(const Key& key) const {
        const Shard& shard = shardFor(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto it = shard.table.find(key);
        if (it == shard.table.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool erase(const Key& key) {
        Shard& shard = shardFor(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        return shard.table.erase(key) > 0;
    }

    bool contains(const Key& key) const {
        const Shard& shard = shardFor(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        return shard.table.find(key) != shard.table.end();
    }

    template <typename UpdateFn>
    bool update(const Key& key, UpdateFn&& fn) {
        Shard& shard = shardFor(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto it = shard.table.find(key);
        if (it == shard.table.end()) {
            return false;
        }
        fn(it->second);
        return true;
    }

    void upsert(const Key& key, const Value& initialValue, const std::function<void(Value&)>& updateFn) {
        Shard& shard = shardFor(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto it = shard.table.find(key);
        if (it == shard.table.end()) {
            shard.table.emplace(key, initialValue);
            return;
        }
        updateFn(it->second);
    }

    std::size_t size() const {
        std::size_t total = 0;
        for (const Shard& shard : shards_) {
            std::lock_guard<std::mutex> lock(shard.mutex);
            total += shard.table.size();
        }
        return total;
    }

    bool empty() const { return size() == 0; }

    std::size_t shardCount() const { return shards_.size(); }

    std::size_t shardIndexFor(const Key& key) const { return Hash{}(key) % shards_.size(); }

private:
    struct Shard {
        mutable std::mutex mutex;
        std::unordered_map<Key, Value> table;
    };

    Shard& shardFor(const Key& key) { return shards_[Hash{}(key) % shards_.size()]; }
    const Shard& shardFor(const Key& key) const { return shards_[Hash{}(key) % shards_.size()]; }

    std::vector<Shard> shards_;
};

}
