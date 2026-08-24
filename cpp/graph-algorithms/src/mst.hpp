#pragma once

#include "graph.hpp"

#include <tuple>
#include <vector>

struct MSTResult {
    std::vector<std::tuple<int, int, double>> edges;
    double totalWeight;
};

MSTResult kruskalMST(const Graph& g);
MSTResult primMST(const Graph& g);
