#pragma once

#include "graph.hpp"

#include <functional>
#include <vector>

struct AStarResult {
    bool found;
    double cost;
    std::vector<int> path;
};

using Heuristic = std::function<double(const Graph&, int, int)>;

AStarResult astar(const Graph& g, int start, int goal, const Heuristic& h);

double euclideanHeuristic(const Graph& g, int a, int b);
double zeroHeuristic(const Graph& g, int a, int b);
