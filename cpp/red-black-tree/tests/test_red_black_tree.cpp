#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../src/red_black_tree.hpp"

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
        rbt::RedBlackTree<int> tree;
        expectTrue(tree.empty(), "new tree is empty");
        expectTrue(!tree.contains(1), "empty tree contains nothing");
        expectTrue(tree.isValidRedBlackTree(), "empty tree is a valid RB tree");
    }

    {
        rbt::RedBlackTree<int> tree;
        expectTrue(tree.insert(10), "first insert succeeds");
        expectTrue(!tree.insert(10), "duplicate insert fails");
        expectEq<std::size_t>(tree.size(), 1, "size reflects unique inserts");
        expectTrue(tree.contains(10), "contains finds inserted key");
    }

    {
        rbt::RedBlackTree<int> tree;
        for (int key : {50, 30, 70, 20, 40, 60, 80, 10, 25, 35, 45}) {
            tree.insert(key);
        }
        expectTrue(tree.isValidRedBlackTree(), "tree stays balanced after inserts");
        auto sorted = tree.inorder();
        expectTrue(std::is_sorted(sorted.begin(), sorted.end()), "inorder traversal is sorted");
        expectEq<std::size_t>(tree.size(), 11, "size matches number of unique keys");
    }

    {
        rbt::RedBlackTree<int> tree;
        for (int i = 1; i <= 1000; ++i) {
            tree.insert(i);
        }
        expectTrue(tree.isValidRedBlackTree(), "sequential insert of 1000 keys stays balanced");
        expectTrue(tree.blackHeight() <= 11, "black height stays logarithmic for 1000 keys");
    }

    {
        rbt::RedBlackTree<int> tree;
        expectTrue(!tree.erase(1), "erase on empty tree fails");
        tree.insert(1);
        expectTrue(tree.erase(1), "erase existing key succeeds");
        expectTrue(tree.empty(), "tree empty after erasing only key");
        expectTrue(!tree.erase(1), "erase again fails");
    }

    {
        std::mt19937 rng(7);
        std::vector<int> keys(500);
        for (int i = 0; i < 500; ++i) {
            keys[i] = i;
        }
        std::shuffle(keys.begin(), keys.end(), rng);

        rbt::RedBlackTree<int> tree;
        std::set<int> reference;
        for (int key : keys) {
            tree.insert(key);
            reference.insert(key);
        }
        expectTrue(tree.isValidRedBlackTree(), "random inserts keep RB invariants");
        expectEq(tree.inorder(), std::vector<int>(reference.begin(), reference.end()),
                 "inorder traversal matches std::set contents");

        std::shuffle(keys.begin(), keys.end(), rng);
        for (int i = 0; i < 250; ++i) {
            tree.erase(keys[i]);
            reference.erase(keys[i]);
        }
        expectTrue(tree.isValidRedBlackTree(), "random erases keep RB invariants");
        expectEq(tree.inorder(), std::vector<int>(reference.begin(), reference.end()),
                 "post-erase traversal matches std::set contents");
        expectEq(tree.size(), reference.size(), "size matches std::set size after erases");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
