#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../src/segment_tree.hpp"

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

class BruteForceReference {
public:
    explicit BruteForceReference(std::vector<long long> data) : data_(std::move(data)) {}

    void rangeAdd(std::size_t left, std::size_t right, long long delta) {
        for (std::size_t i = left; i <= right; ++i) {
            data_[i] += delta;
        }
    }

    long long rangeSum(std::size_t left, std::size_t right) const {
        long long sum = 0;
        for (std::size_t i = left; i <= right; ++i) {
            sum += data_[i];
        }
        return sum;
    }

    long long rangeMin(std::size_t left, std::size_t right) const {
        long long minValue = data_[left];
        for (std::size_t i = left; i <= right; ++i) {
            minValue = std::min(minValue, data_[i]);
        }
        return minValue;
    }

private:
    std::vector<long long> data_;
};

}

int main() {
    {
        std::vector<long long> data = {1, 2, 3, 4, 5};
        segtree::SumSegmentTree sumTree(data);
        segtree::MinSegmentTree minTree(data);

        expectEq<long long>(sumTree.rangeQuery(0, 4), 15, "sum of whole array");
        expectEq<long long>(sumTree.rangeQuery(1, 3), 9, "sum of subrange");
        expectEq<long long>(sumTree.rangeQuery(2, 2), 3, "sum of single element");
        expectEq<long long>(minTree.rangeQuery(0, 4), 1, "min of whole array");
        expectEq<long long>(minTree.rangeQuery(2, 4), 3, "min of subrange");
        expectEq<long long>(minTree.rangeQuery(4, 4), 5, "min of single element");
    }

    {
        std::vector<long long> data = {5, -2, 8, 0, 3, 9, -7, 4};
        segtree::SumSegmentTree sumTree(data);
        segtree::MinSegmentTree minTree(data);

        sumTree.rangeUpdate(1, 5, 10);
        minTree.rangeUpdate(1, 5, 10);

        expectEq<long long>(sumTree.rangeQuery(0, 7), 20 + 10 * 5, "sum after range update reflects delta");
        expectEq<long long>(sumTree.rangeQuery(1, 5), 18 + 10 * 5, "sum of updated subrange");
        expectEq<long long>(sumTree.rangeQuery(6, 7), -3, "sum outside update range is unchanged");
        expectEq<long long>(minTree.rangeQuery(1, 5), -2 + 10, "min of updated subrange reflects delta");
        expectEq<long long>(minTree.rangeQuery(6, 7), -7, "min outside update range is unchanged");

        minTree.rangeUpdate(0, 7, -3);
        sumTree.rangeUpdate(0, 7, -3);
        expectEq<long long>(minTree.rangeQuery(0, 7), -7 - 3, "min after second overlapping update");
        expectEq<long long>(sumTree.rangeQuery(0, 7), 20 + 10 * 5 - 3 * 8, "sum after second overlapping update");
    }

    {
        std::mt19937 rng(1234);
        std::uniform_int_distribution<long long> valueDist(-500, 500);
        std::uniform_int_distribution<long long> deltaDist(-100, 100);

        const std::size_t n = 300;
        std::vector<long long> initial(n);
        for (std::size_t i = 0; i < n; ++i) {
            initial[i] = valueDist(rng);
        }

        segtree::SumSegmentTree sumTree(initial);
        segtree::MinSegmentTree minTree(initial);
        BruteForceReference reference(initial);

        std::uniform_int_distribution<std::size_t> indexDist(0, n - 1);
        std::uniform_int_distribution<int> opDist(0, 2);

        bool sumMatches = true;
        bool minMatches = true;

        const int iterations = 5000;
        for (int iter = 0; iter < iterations; ++iter) {
            std::size_t a = indexDist(rng);
            std::size_t b = indexDist(rng);
            std::size_t left = std::min(a, b);
            std::size_t right = std::max(a, b);

            int op = opDist(rng);
            if (op == 0) {
                long long delta = deltaDist(rng);
                sumTree.rangeUpdate(left, right, delta);
                minTree.rangeUpdate(left, right, delta);
                reference.rangeAdd(left, right, delta);
            } else if (op == 1) {
                if (sumTree.rangeQuery(left, right) != reference.rangeSum(left, right)) {
                    sumMatches = false;
                }
            } else {
                if (minTree.rangeQuery(left, right) != reference.rangeMin(left, right)) {
                    minMatches = false;
                }
            }
        }

        expectTrue(sumMatches, "random stress: range sum matches brute force over 5000 operations");
        expectTrue(minMatches, "random stress: range min matches brute force over 5000 operations");

        bool finalSumMatches = true;
        bool finalMinMatches = true;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i; j < n; j += 37) {
                if (sumTree.rangeQuery(i, j) != reference.rangeSum(i, j)) {
                    finalSumMatches = false;
                }
                if (minTree.rangeQuery(i, j) != reference.rangeMin(i, j)) {
                    finalMinMatches = false;
                }
            }
        }
        expectTrue(finalSumMatches, "post-stress sweep: range sum matches brute force for many subranges");
        expectTrue(finalMinMatches, "post-stress sweep: range min matches brute force for many subranges");
    }

    {
        std::vector<long long> single = {42};
        segtree::SumSegmentTree sumTree(single);
        segtree::MinSegmentTree minTree(single);
        expectEq<long long>(sumTree.rangeQuery(0, 0), 42, "single element sum");
        expectEq<long long>(minTree.rangeQuery(0, 0), 42, "single element min");
        sumTree.rangeUpdate(0, 0, 8);
        minTree.rangeUpdate(0, 0, 8);
        expectEq<long long>(sumTree.rangeQuery(0, 0), 50, "single element sum after update");
        expectEq<long long>(minTree.rangeQuery(0, 0), 50, "single element min after update");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
