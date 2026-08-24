#include "scc.hpp"

#include <algorithm>

namespace {

struct TarjanState {
    const Graph& g;
    std::vector<int> index;
    std::vector<int> lowlink;
    std::vector<bool> onStack;
    std::vector<int> stack;
    int counter = 0;
    std::vector<std::vector<int>> components;

    explicit TarjanState(const Graph& graph)
        : g(graph),
          index(graph.numVertices(), -1),
          lowlink(graph.numVertices(), -1),
          onStack(graph.numVertices(), false) {}

    void strongconnect(int v) {
        index[v] = counter;
        lowlink[v] = counter;
        counter++;
        stack.push_back(v);
        onStack[v] = true;

        for (const auto& e : g.neighbors(v)) {
            int w = e.to;
            if (index[w] == -1) {
                strongconnect(w);
                lowlink[v] = std::min(lowlink[v], lowlink[w]);
            } else if (onStack[w]) {
                lowlink[v] = std::min(lowlink[v], index[w]);
            }
        }

        if (lowlink[v] == index[v]) {
            std::vector<int> component;
            int w;
            do {
                w = stack.back();
                stack.pop_back();
                onStack[w] = false;
                component.push_back(w);
            } while (w != v);
            components.push_back(component);
        }
    }
};

}

std::vector<std::vector<int>> tarjanSCC(const Graph& g) {
    TarjanState state(g);
    for (int v = 0; v < g.numVertices(); v++) {
        if (state.index[v] == -1) state.strongconnect(v);
    }
    return state.components;
}
