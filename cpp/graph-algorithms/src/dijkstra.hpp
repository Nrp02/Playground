#pragma once

#include "graph.hpp"

#include <limits>
#include <vector>

struct DijkstraResult {
    std::vector<double> dist;
    std::vector<int> parent;
};

constexpr double kInfinity = std::numeric_limits<double>::infinity();

DijkstraResult dijkstra(const Graph& g, int start);
std::vector<int> reconstructPath(const std::vector<int>& parent, int start, int target);
