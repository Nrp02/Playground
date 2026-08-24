#include "graph.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

Graph::Graph(int n, bool directed) : n_(n), directed_(directed), adj_(n) {}

void Graph::addEdge(int u, int v, double weight) {
    adj_[u].push_back({v, weight});
    if (!directed_) {
        adj_[v].push_back({u, weight});
    }
    edges_.emplace_back(u, v, weight);
}

void Graph::setCoords(int v, double x, double y) {
    coords_[v] = {x, y};
    hasCoords_ = true;
}

int Graph::numVertices() const { return n_; }
bool Graph::isDirected() const { return directed_; }
bool Graph::hasCoords() const { return hasCoords_; }

const std::vector<Edge>& Graph::neighbors(int u) const { return adj_[u]; }

const std::vector<std::tuple<int, int, double>>& Graph::edgeList() const { return edges_; }

std::pair<double, double> Graph::coords(int v) const {
    auto it = coords_.find(v);
    if (it == coords_.end()) return {0.0, 0.0};
    return it->second;
}

Graph Graph::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open graph file: " + path);

    int n;
    int directedFlag;
    in >> n >> directedFlag;

    Graph g(n, directedFlag != 0);

    std::string line;
    std::getline(in, line);

    bool readingCoords = false;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line == "COORDS") {
            readingCoords = true;
            continue;
        }
        std::istringstream iss(line);
        if (readingCoords) {
            int id;
            double x, y;
            iss >> id >> x >> y;
            g.setCoords(id, x, y);
        } else {
            int u, v;
            double w;
            iss >> u >> v >> w;
            g.addEdge(u, v, w);
        }
    }

    return g;
}
