#pragma once

#include "graph.hpp"

#include <vector>

std::vector<int> bfsOrder(const Graph& g, int start);
std::vector<int> bfsPath(const Graph& g, int start, int target);
std::vector<int> dfsOrder(const Graph& g, int start);
std::vector<int> dfsPath(const Graph& g, int start, int target);
