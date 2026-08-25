#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../src/skip_list.hpp"

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
        sl::SkipList<int> list;
        expectTrue(list.empty(), "new list is empty");
        expectTrue(!list.contains(1), "empty list contains nothing");
        expectEq<std::size_t>(list.size(), 0, "empty list has size 0");
        expectTrue(list.range(0, 10).empty(), "empty list range is empty");
    }

    {
        sl::SkipList<int> list;
        expectTrue(list.insert(10), "first insert succeeds");
        expectTrue(!list.insert(10), "duplicate insert fails");
        expectEq<std::size_t>(list.size(), 1, "size reflects unique inserts");
        expectTrue(list.contains(10), "contains finds inserted key");
        expectTrue(!list.contains(11), "contains rejects missing key");
        auto singleRange = list.range(0, 100);
        expectEq(singleRange, std::vector<int>{10}, "single element range");
    }

    {
        sl::SkipList<int> list;
        for (int key : {50, 30, 70, 20, 40, 60, 80, 10, 25, 35, 45}) {
            list.insert(key);
        }
        auto sorted = list.toVector();
        expectTrue(std::is_sorted(sorted.begin(), sorted.end()), "toVector is sorted");
        expectEq<std::size_t>(list.size(), 11, "size matches number of unique keys");
        auto ranged = list.range(25, 60);
        std::vector<int> expected = {25, 30, 35, 40, 45, 50, 60};
        expectEq(ranged, expected, "range query returns correct bounds inclusive");
    }

    {
        sl::SkipList<int> list;
        expectTrue(!list.erase(1), "erase on empty list fails");
        list.insert(1);
        expectTrue(list.erase(1), "erase existing key succeeds");
        expectTrue(list.empty(), "list empty after erasing only key");
        expectTrue(!list.erase(1), "erase again fails");
    }

    {
        std::mt19937 rng(7);
        std::vector<int> keys(500);
        for (int i = 0; i < 500; ++i) {
            keys[i] = i;
        }
        std::shuffle(keys.begin(), keys.end(), rng);

        sl::SkipList<int> list;
        std::set<int> reference;
        for (int key : keys) {
            list.insert(key);
            reference.insert(key);
        }
        expectEq(list.toVector(), std::vector<int>(reference.begin(), reference.end()),
                 "toVector matches std::set contents after random inserts");

        auto ranged = list.range(100, 199);
        std::vector<int> expectedRange;
        for (auto it = reference.lower_bound(100); it != reference.end() && *it <= 199; ++it) {
            expectedRange.push_back(*it);
        }
        expectEq(ranged, expectedRange, "random-insert range matches std::set reference");

        std::shuffle(keys.begin(), keys.end(), rng);
        for (int i = 0; i < 250; ++i) {
            expectTrue(list.erase(keys[i]) == (reference.count(keys[i]) > 0),
                       "erase return value matches reference presence");
            reference.erase(keys[i]);
        }
        expectEq(list.toVector(), std::vector<int>(reference.begin(), reference.end()),
                 "toVector matches std::set contents after random erases");
        expectEq(list.size(), reference.size(), "size matches std::set size after erases");

        for (int key : keys) {
            expectTrue(list.contains(key) == (reference.count(key) > 0),
                       "contains matches reference for all original keys");
        }
    }

    {
        std::mt19937 rng(123);
        std::set<int> reference;
        sl::SkipList<int> list;
        std::uniform_int_distribution<int> keyDist(0, 999);
        for (int op = 0; op < 3000; ++op) {
            int key = keyDist(rng);
            int action = op % 3;
            if (action == 0) {
                bool inserted = list.insert(key);
                bool expected = reference.insert(key).second;
                expectEq(inserted, expected, "randomized insert matches std::set");
            } else if (action == 1) {
                bool erased = list.erase(key);
                bool expected = reference.erase(key) > 0;
                expectEq(erased, expected, "randomized erase matches std::set");
            } else {
                bool found = list.contains(key);
                bool expected = reference.count(key) > 0;
                expectEq(found, expected, "randomized contains matches std::set");
            }
        }
        expectEq(list.toVector(), std::vector<int>(reference.begin(), reference.end()),
                 "final state matches std::set after randomized operations");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
