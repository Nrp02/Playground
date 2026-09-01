#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../src/interval_tree.hpp"

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

struct BruteEntry {
    long long low;
    long long high;
    std::uint64_t id;
    int payload;
};

bool bruteOverlaps(const BruteEntry& e, long long qlow, long long qhigh) {
    return e.low <= qhigh && qlow <= e.high;
}

std::vector<BruteEntry> bruteOverlapping(const std::vector<BruteEntry>& entries, long long qlow, long long qhigh) {
    std::vector<BruteEntry> out;
    for (const auto& e : entries) {
        if (bruteOverlaps(e, qlow, qhigh)) {
            out.push_back(e);
        }
    }
    return out;
}

std::size_t sortedIds(std::vector<std::uint64_t> ids) {
    std::sort(ids.begin(), ids.end());
    return ids.size();
}

bool sameIdSet(std::vector<itree::Interval<int>> a, std::vector<BruteEntry> b) {
    std::vector<std::uint64_t> idsA;
    std::vector<std::uint64_t> idsB;
    for (const auto& iv : a) {
        idsA.push_back(iv.id);
    }
    for (const auto& e : b) {
        idsB.push_back(e.id);
    }
    std::sort(idsA.begin(), idsA.end());
    std::sort(idsB.begin(), idsB.end());
    return idsA == idsB;
}

}

int main() {
    {
        itree::IntervalTree<int> tree;
        std::size_t visited = 0;
        expectTrue(tree.empty(), "empty tree is empty");
        expectTrue(tree.isValid(), "empty tree is valid");
        expectEq<std::size_t>(tree.overlapping(0, 10, visited).size(), 0, "empty tree overlapping returns nothing");
        expectEq<std::size_t>(tree.countOverlapping(0, 10, visited), 0, "empty tree count is zero");
        expectTrue(!tree.anyOverlap(0, 10, visited), "empty tree anyOverlap is false");
        expectTrue(!tree.remove(999), "remove on empty tree fails");
    }

    {
        itree::IntervalTree<int> tree;
        auto id1 = tree.insert(1, 5, 100);
        auto id2 = tree.insert(5, 9, 200);
        std::size_t visited = 0;
        expectTrue(tree.anyOverlap(1, 5, visited), "closed interval [1,5] overlaps [5,9] at touch point");
        auto touchResult = tree.overlapping(5, 5, visited);
        expectEq<std::size_t>(touchResult.size(), 2, "stab at exact touch point 5 finds both intervals");
        expectTrue(id1 != id2, "distinct intervals get distinct ids");
        auto stabFive = tree.stab(5, visited);
        expectEq<std::size_t>(stabFive.size(), 2, "stab(5) matches [1,5] and [5,9]");
        auto stabZero = tree.stab(0, visited);
        expectEq<std::size_t>(stabZero.size(), 0, "stab(0) matches nothing");
        auto stabTen = tree.stab(10, visited);
        expectEq<std::size_t>(stabTen.size(), 0, "stab(10) matches nothing");
    }

    {
        itree::IntervalTree<int> tree;
        auto id = tree.insert(3, 3, 1);
        std::size_t visited = 0;
        auto result = tree.stab(3, visited);
        expectEq<std::size_t>(result.size(), 1, "zero-length interval [3,3] stabbed at 3");
        expectTrue(tree.anyOverlap(2, 4, visited), "zero-length interval overlaps a containing range");
        expectTrue(tree.isValid(), "tree with zero-length interval is valid");
        expectTrue(tree.remove(id), "remove zero-length interval succeeds");
        expectTrue(tree.empty(), "tree empty after removing only interval");
    }

    {
        itree::IntervalTree<int> tree;
        auto id1 = tree.insert(4, 8, 1);
        auto id2 = tree.insert(4, 8, 2);
        auto id3 = tree.insert(4, 8, 3);
        expectEq<std::size_t>(tree.size(), 3, "identical duplicate intervals all stored");
        std::size_t visited = 0;
        auto result = tree.stab(5, visited);
        expectEq<std::size_t>(result.size(), 3, "stab finds all identical duplicates");
        expectTrue(tree.isValid(), "tree with duplicates is valid");
        expectTrue(tree.remove(id2), "remove one duplicate by id succeeds");
        expectEq<std::size_t>(tree.size(), 2, "size decreases after removing one duplicate");
        result = tree.stab(5, visited);
        expectEq<std::size_t>(result.size(), 2, "stab finds remaining duplicates after one removed");
        expectTrue(tree.isValid(), "tree with one duplicate removed still valid");
        expectTrue(tree.remove(id1) && tree.remove(id3), "remove remaining duplicates succeeds");
        expectTrue(tree.empty(), "tree empty after removing all duplicates");
    }

    {
        itree::IntervalTree<int> tree;
        tree.insert(1, 2, 1);
        expectTrue(!tree.remove(9999), "remove of non-existent id fails");
        expectEq<std::size_t>(tree.size(), 1, "size unaffected by failed remove");
    }

    {
        std::mt19937 rng(123);
        std::uniform_int_distribution<long long> lowDist(0, 200);
        std::uniform_int_distribution<long long> lenDist(0, 20);
        std::uniform_int_distribution<long long> queryLowDist(0, 200);
        std::uniform_int_distribution<long long> queryLenDist(0, 30);

        for (int trial = 0; trial < 30; ++trial) {
            itree::IntervalTree<int> tree;
            std::vector<BruteEntry> brute;
            int count = 200;
            for (int i = 0; i < count; ++i) {
                long long low = lowDist(rng);
                long long high = low + lenDist(rng);
                auto id = tree.insert(low, high, i);
                brute.push_back(BruteEntry{low, high, id, i});
                expectTrue(tree.isValid(), "tree valid after each insert in random trial");
            }

            for (int q = 0; q < 20; ++q) {
                long long qlow = queryLowDist(rng);
                long long qhigh = qlow + queryLenDist(rng);
                std::size_t visited = 0;
                auto treeResult = tree.overlapping(qlow, qhigh, visited);
                auto bruteResult = bruteOverlapping(brute, qlow, qhigh);
                expectTrue(sameIdSet(treeResult, bruteResult), "overlapping matches brute force reference");
                expectEq<bool>(tree.anyOverlap(qlow, qhigh, visited), !bruteResult.empty(), "anyOverlap matches brute force reference");
                expectEq<std::size_t>(tree.countOverlapping(qlow, qhigh, visited), bruteResult.size(), "countOverlapping matches brute force reference");

                long long point = queryLowDist(rng);
                auto stabResult = tree.stab(point, visited);
                auto bruteStab = bruteOverlapping(brute, point, point);
                expectTrue(sameIdSet(stabResult, bruteStab), "stab matches brute force reference");
            }

            std::shuffle(brute.begin(), brute.end(), rng);
            int deleteCount = count / 2;
            for (int i = 0; i < deleteCount; ++i) {
                expectTrue(tree.remove(brute[i].id), "delete of existing interval succeeds in random trial");
                expectTrue(tree.isValid(), "tree valid after each delete in random trial");
            }
            expectEq<std::size_t>(tree.size(), static_cast<std::size_t>(count - deleteCount), "size correct after batch deletes");

            std::vector<BruteEntry> remaining(brute.begin() + deleteCount, brute.end());
            for (int q = 0; q < 10; ++q) {
                long long qlow = queryLowDist(rng);
                long long qhigh = qlow + queryLenDist(rng);
                std::size_t visited = 0;
                auto treeResult = tree.overlapping(qlow, qhigh, visited);
                auto bruteResult = bruteOverlapping(remaining, qlow, qhigh);
                expectTrue(sameIdSet(treeResult, bruteResult), "overlapping after deletes matches brute force reference");
            }
        }
    }

    {
        itree::IntervalTree<int> tree;
        std::vector<std::uint64_t> ids;
        for (int i = 0; i < 3000; ++i) {
            ids.push_back(tree.insert(i, i + (i % 7), i));
        }
        expectTrue(tree.isValid(), "large tree valid after bulk insert");
        std::mt19937 rng(55);
        std::shuffle(ids.begin(), ids.end(), rng);
        for (auto id : ids) {
            expectTrue(tree.remove(id), "bulk delete of every interval succeeds");
        }
        expectTrue(tree.empty(), "tree empty after deleting everything");
        expectTrue(tree.isValid(), "tree valid (trivially) after deleting everything");
        expectEq<std::size_t>(tree.size(), 0, "size zero after deleting everything");
        std::size_t visited = 0;
        expectEq<std::size_t>(tree.overlapping(0, 10000, visited).size(), 0, "no matches remain after deleting everything");
    }

    {
        itree::IntervalTree<int> tree;
        for (int i = 0; i < 1000; ++i) {
            tree.insert(i * 2, i * 2 + 1, i);
        }
        for (int i = 0; i < 300; ++i) {
            tree.insert(500, 900, 1000 + i);
        }
        std::size_t visited = 0;
        tree.overlapping(500, 500, visited);
        expectTrue(visited < tree.size(), "pruning visits fewer nodes than total on a sparse-side query");
        expectTrue(tree.isValid(), "mixed dense/sparse tree is valid");
    }

    {
        std::vector<itree::Booking<std::string>> bookings{
            {1, 3, "a"},
            {2, 4, "b"},
            {5, 7, "c"},
            {6, 9, "d"},
            {10, 12, "e"},
        };
        auto conflicts = itree::findConflicts(bookings);
        expectEq<std::size_t>(conflicts.size(), 2, "findConflicts finds exactly the overlapping pairs");
        auto schedule = itree::maxNonOverlapping(bookings);
        expectEq<std::size_t>(schedule.size(), 3, "maxNonOverlapping picks the classic greedy-by-end-time optimum");
        for (std::size_t i = 1; i < schedule.size(); ++i) {
            expectTrue(schedule[i].low > schedule[i - 1].high, "scheduled bookings do not overlap under closed semantics");
        }
    }

    std::cout << "\n" << (g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
