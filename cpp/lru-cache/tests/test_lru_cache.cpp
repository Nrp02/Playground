#include <iostream>
#include <string>

#include "../src/lru_cache.hpp"

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

}

int main() {
    {
        lru::LRUCache<int, std::string> cache(2);
        std::string value;
        expectTrue(!cache.get(1, value), "get on empty cache misses");
        expectEq<std::size_t>(cache.size(), 0, "empty cache has size 0");
    }

    {
        lru::LRUCache<int, std::string> cache(2);
        cache.put(1, "a");
        cache.put(2, "b");
        std::string value;
        expectTrue(cache.get(1, value), "get hits after put");
        expectEq(value, std::string("a"), "get returns the stored value");
        expectEq<std::size_t>(cache.size(), 2, "size reflects number of entries");
    }

    {
        lru::LRUCache<int, std::string> cache(2);
        cache.put(1, "a");
        cache.put(2, "b");
        cache.put(3, "c");
        expectTrue(!cache.contains(1), "least recently used entry is evicted");
        expectTrue(cache.contains(2), "second entry survives eviction");
        expectTrue(cache.contains(3), "newest entry survives eviction");
        expectEq<std::size_t>(cache.size(), 2, "size stays at capacity after eviction");
    }

    {
        lru::LRUCache<int, std::string> cache(2);
        cache.put(1, "a");
        cache.put(2, "b");
        std::string value;
        cache.get(1, value);
        cache.put(3, "c");
        expectTrue(cache.contains(1), "recently accessed entry survives eviction");
        expectTrue(!cache.contains(2), "untouched entry is evicted instead");
    }

    {
        lru::LRUCache<int, std::string> cache(3);
        cache.put(1, "a");
        cache.put(1, "b");
        std::string value;
        cache.get(1, value);
        expectEq(value, std::string("b"), "put on existing key overwrites value");
        expectEq<std::size_t>(cache.size(), 1, "put on existing key does not grow size");
    }

    {
        lru::LRUCache<int, std::string> cache(3);
        cache.put(1, "a");
        cache.put(2, "b");
        expectTrue(cache.erase(1), "erase removes an existing key");
        expectTrue(!cache.contains(1), "erased key is no longer present");
        expectTrue(!cache.erase(999), "erase on missing key returns false");
        expectEq<std::size_t>(cache.size(), 1, "size reflects the erase");
    }

    {
        lru::LRUCache<int, int> cache(10);
        for (int i = 0; i < 10; ++i) {
            cache.put(i, i);
        }
        int value = 0;
        for (int i = 0; i < 5; ++i) {
            cache.get(i, value);
        }
        for (int i = 100; i < 103; ++i) {
            cache.get(i, value);
        }
        expectEq<std::size_t>(cache.hits(), 5, "hit counter tracks successful lookups");
        expectEq<std::size_t>(cache.misses(), 3, "miss counter tracks failed lookups");
    }

    {
        lru::LRUCache<int, int> cache(1);
        cache.put(1, 100);
        cache.put(2, 200);
        expectTrue(!cache.contains(1), "capacity-1 cache evicts the only prior entry");
        expectTrue(cache.contains(2), "capacity-1 cache keeps the newest entry");
    }

    {
        lru::LRUCache<int, int> cache(4);
        cache.put(1, 1);
        cache.put(2, 2);
        cache.clear();
        expectEq<std::size_t>(cache.size(), 0, "clear empties the cache");
        expectTrue(!cache.contains(1), "clear removes all keys");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
