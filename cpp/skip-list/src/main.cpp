#include <algorithm>
#include <iostream>
#include <random>
#include <vector>

#include "skip_list.hpp"

int main() {
    sl::SkipList<int> list(0.5, 16, 42);

    std::cout << "=== insert a batch of keys ===\n";
    std::mt19937 rng(42);
    std::vector<int> keys(200);
    for (int i = 0; i < 200; ++i) {
        keys[i] = i * 3;
    }
    std::shuffle(keys.begin(), keys.end(), rng);
    for (int key : keys) {
        list.insert(key);
    }
    std::cout << "size=" << list.size() << " level=" << list.currentLevel() << "\n";

    std::cout << "\n=== point lookups ===\n";
    for (int key : {0, 3, 300, 301, 597}) {
        std::cout << "contains(" << key << ") = " << std::boolalpha << list.contains(key) << "\n";
    }

    std::cout << "\n=== range query [100, 130] ===\n";
    auto rangeResult = list.range(100, 130);
    for (int v : rangeResult) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    std::cout << "\n=== delete every fifth key ===\n";
    for (std::size_t i = 0; i < keys.size(); i += 5) {
        list.erase(keys[i]);
    }
    std::cout << "size=" << list.size() << "\n";

    auto sorted = list.toVector();
    std::cout << "remaining is sorted: " << std::is_sorted(sorted.begin(), sorted.end()) << "\n";
    std::cout << "remaining count: " << sorted.size() << "\n";

    return 0;
}
