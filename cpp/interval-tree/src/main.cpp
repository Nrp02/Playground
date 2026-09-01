#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "interval_tree.hpp"

namespace {

std::vector<itree::Interval<int>> naiveOverlapping(const std::vector<itree::Interval<int>>& intervals, long long qlow, long long qhigh) {
    std::vector<itree::Interval<int>> result;
    for (const auto& iv : intervals) {
        if (iv.low <= qhigh && qlow <= iv.high) {
            result.push_back(iv);
        }
    }
    return result;
}

}

int main() {
    std::mt19937 rng(7);
    std::uniform_int_distribution<long long> lowDist(0, 1'000'000);
    std::uniform_int_distribution<long long> lenDist(1, 500);
    std::uniform_int_distribution<long long> clusterLowDist(500'000, 500'200);
    std::uniform_int_distribution<long long> clusterLenDist(1, 4'000);

    itree::IntervalTree<int> tree;
    std::vector<itree::Interval<int>> flat;

    constexpr int kRandomCount = 40'000;
    constexpr int kClusterCount = 5'000;

    for (int i = 0; i < kRandomCount; ++i) {
        long long low = lowDist(rng);
        long long high = low + lenDist(rng);
        auto id = tree.insert(low, high, i);
        flat.push_back(itree::Interval<int>{low, high, id, i});
    }
    for (int i = 0; i < kClusterCount; ++i) {
        long long low = clusterLowDist(rng);
        long long high = low + clusterLenDist(rng);
        auto id = tree.insert(low, high, kRandomCount + i);
        flat.push_back(itree::Interval<int>{low, high, id, kRandomCount + i});
    }

    std::cout << "=== interval tree built ===\n";
    std::cout << "total intervals=" << tree.size() << " height=" << tree.height() << " valid=" << std::boolalpha << tree.isValid() << "\n\n";

    std::size_t visited = 0;
    long long stabPoint = 500'100;
    auto stabResult = tree.stab(stabPoint, visited);
    std::cout << "=== stab(" << stabPoint << ") ===\n";
    std::cout << "matches=" << stabResult.size() << " nodesVisited=" << visited << " totalNodes=" << tree.size() << "\n\n";

    long long qlow = 499'900;
    long long qhigh = 500'300;
    visited = 0;
    auto overlapResult = tree.overlapping(qlow, qhigh, visited);
    std::cout << "=== overlapping(" << qlow << ", " << qhigh << ") ===\n";
    std::cout << "matches=" << overlapResult.size() << " nodesVisited=" << visited << " totalNodes=" << tree.size() << "\n\n";

    visited = 0;
    bool any = tree.anyOverlap(qlow, qhigh, visited);
    std::cout << "=== anyOverlap(" << qlow << ", " << qhigh << ") ===\n";
    std::cout << "found=" << any << " nodesVisited=" << visited << "\n\n";

    visited = 0;
    std::size_t count = tree.countOverlapping(qlow, qhigh, visited);
    std::cout << "=== countOverlapping(" << qlow << ", " << qhigh << ") ===\n";
    std::cout << "count=" << count << " nodesVisited=" << visited << "\n\n";

    auto start = std::chrono::steady_clock::now();
    visited = 0;
    auto treeResult = tree.overlapping(qlow, qhigh, visited);
    auto mid = std::chrono::steady_clock::now();
    auto naiveResult = naiveOverlapping(flat, qlow, qhigh);
    auto end = std::chrono::steady_clock::now();

    auto treeMicros = std::chrono::duration_cast<std::chrono::microseconds>(mid - start).count();
    auto naiveMicros = std::chrono::duration_cast<std::chrono::microseconds>(end - mid).count();

    std::cout << "=== tree vs naive linear scan ===\n";
    std::cout << "tree: " << treeResult.size() << " matches in " << treeMicros << " us, nodesVisited=" << visited << "/" << tree.size() << "\n";
    std::cout << "naive: " << naiveResult.size() << " matches in " << naiveMicros << " us, scanned=" << flat.size() << "/" << flat.size() << "\n\n";

    std::cout << "=== interval scheduling / conflict detection demo ===\n";
    std::vector<itree::Booking<std::string>> bookings{
        {9, 11, "Room A: standup"},
        {10, 12, "Room A: design review"},
        {12, 13, "Room A: lunch sync"},
        {13, 15, "Room A: sprint planning"},
        {14, 16, "Room A: 1:1"},
        {16, 18, "Room A: retro"},
        {8, 9, "Room A: early sync"},
    };

    auto conflicts = itree::findConflicts(bookings);
    std::cout << "conflicts found: " << conflicts.size() << "\n";
    for (const auto& [a, b] : conflicts) {
        std::cout << "  [" << a.low << "," << a.high << "] \"" << a.payload << "\" overlaps ["
                  << b.low << "," << b.high << "] \"" << b.payload << "\"\n";
    }

    auto schedule = itree::maxNonOverlapping(bookings);
    std::cout << "\nmax non-overlapping schedule (" << schedule.size() << " bookings):\n";
    for (const auto& booking : schedule) {
        std::cout << "  [" << booking.low << "," << booking.high << "] \"" << booking.payload << "\"\n";
    }

    return 0;
}
