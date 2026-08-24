#include <algorithm>
#include <iostream>
#include <random>
#include <vector>

#include "red_black_tree.hpp"

int main() {
    rbt::RedBlackTree<int> tree;

    std::cout << "=== sequential insert of 1..1000 ===\n";
    for (int i = 1; i <= 1000; ++i) {
        tree.insert(i);
    }
    std::cout << "size=" << tree.size() << " blackHeight=" << tree.blackHeight()
              << " valid=" << std::boolalpha << tree.isValidRedBlackTree() << "\n";

    std::cout << "\n=== random insert/erase of 2000 keys ===\n";
    std::mt19937 rng(42);
    std::vector<int> keys(2000);
    for (int i = 0; i < 2000; ++i) {
        keys[i] = i;
    }
    std::shuffle(keys.begin(), keys.end(), rng);

    rbt::RedBlackTree<int> randomTree;
    for (int key : keys) {
        randomTree.insert(key);
    }
    std::cout << "after inserts: size=" << randomTree.size()
              << " blackHeight=" << randomTree.blackHeight()
              << " valid=" << randomTree.isValidRedBlackTree() << "\n";

    std::shuffle(keys.begin(), keys.end(), rng);
    for (int i = 0; i < 1000; ++i) {
        randomTree.erase(keys[i]);
    }
    std::cout << "after erasing half: size=" << randomTree.size()
              << " blackHeight=" << randomTree.blackHeight()
              << " valid=" << randomTree.isValidRedBlackTree() << "\n";

    auto sorted = randomTree.inorder();
    std::cout << "inorder is sorted: " << std::is_sorted(sorted.begin(), sorted.end()) << "\n";

    return 0;
}
