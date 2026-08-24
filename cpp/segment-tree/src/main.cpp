#include <iostream>
#include <random>
#include <vector>

#include "segment_tree.hpp"

int main() {
    const std::size_t n = 5000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<long long> valueDist(-1000, 1000);

    std::vector<long long> data(n);
    for (std::size_t i = 0; i < n; ++i) {
        data[i] = valueDist(rng);
    }

    segtree::SumSegmentTree sumTree(data);
    segtree::MinSegmentTree minTree(data);

    std::cout << "=== built segment trees over " << n << " elements ===\n";

    std::size_t left = 1000;
    std::size_t right = 3500;

    std::cout << "\n=== before update ===\n";
    std::cout << "range sum [" << left << ", " << right << "] = " << sumTree.rangeQuery(left, right) << "\n";
    std::cout << "range min [" << left << ", " << right << "] = " << minTree.rangeQuery(left, right) << "\n";
    std::cout << "range sum [0, " << (n - 1) << "] = " << sumTree.rangeQuery(0, n - 1) << "\n";
    std::cout << "range min [0, " << (n - 1) << "] = " << minTree.rangeQuery(0, n - 1) << "\n";

    long long delta = 250;
    std::size_t updateLeft = 500;
    std::size_t updateRight = 2500;
    sumTree.rangeUpdate(updateLeft, updateRight, delta);
    minTree.rangeUpdate(updateLeft, updateRight, delta);

    std::cout << "\n=== applied range update: add " << delta << " to [" << updateLeft << ", " << updateRight
              << "] ===\n";

    std::cout << "\n=== after update ===\n";
    std::cout << "range sum [" << left << ", " << right << "] = " << sumTree.rangeQuery(left, right) << "\n";
    std::cout << "range min [" << left << ", " << right << "] = " << minTree.rangeQuery(left, right) << "\n";
    std::cout << "range sum [0, " << (n - 1) << "] = " << sumTree.rangeQuery(0, n - 1) << "\n";
    std::cout << "range min [0, " << (n - 1) << "] = " << minTree.rangeQuery(0, n - 1) << "\n";

    std::size_t untouchedLeft = 4000;
    std::size_t untouchedRight = 4500;
    std::cout << "\n=== untouched range [" << untouchedLeft << ", " << untouchedRight << "] (should be unchanged) ===\n";
    std::cout << "range sum = " << sumTree.rangeQuery(untouchedLeft, untouchedRight) << "\n";
    std::cout << "range min = " << minTree.rangeQuery(untouchedLeft, untouchedRight) << "\n";

    return 0;
}
