#include <iostream>
#include <string>

#include "lru_cache.hpp"

void demoBasicEviction() {
    std::cout << "=== basic eviction (capacity 3) ===\n";
    lru::LRUCache<int, std::string> cache(3);
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    std::cout << "size after 3 puts: " << cache.size() << "\n";

    cache.put(4, "four");
    std::cout << "size after 4th put: " << cache.size() << "\n";
    std::cout << "contains(1): " << std::boolalpha << cache.contains(1) << "\n";
    std::cout << "contains(4): " << std::boolalpha << cache.contains(4) << "\n";
}

void demoAccessOrder() {
    std::cout << "\n=== access order affects eviction (capacity 2) ===\n";
    lru::LRUCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);

    int value = 0;
    cache.get("a", value);

    cache.put("c", 3);
    std::cout << "contains(a) after touching it: " << std::boolalpha << cache.contains("a") << "\n";
    std::cout << "contains(b) after it became LRU: " << std::boolalpha << cache.contains("b") << "\n";
}

void demoHitRate() {
    std::cout << "\n=== hit rate tracking ===\n";
    lru::LRUCache<int, int> cache(100);
    for (int i = 0; i < 100; ++i) {
        cache.put(i, i * i);
    }
    int value = 0;
    for (int i = 0; i < 100; ++i) {
        cache.get(i, value);
    }
    for (int i = 100; i < 150; ++i) {
        cache.get(i, value);
    }
    std::cout << "hits=" << cache.hits() << " misses=" << cache.misses() << " hitRate=" << cache.hitRate() << "\n";
}

int main() {
    demoBasicEviction();
    demoAccessOrder();
    demoHitRate();
    return 0;
}
