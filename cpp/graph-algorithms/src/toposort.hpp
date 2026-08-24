#pragma once

#include "graph.hpp"

#include <vector>

std::vector<int> topoSort(const Graph& g, bool& hasCycle);
