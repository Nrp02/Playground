#pragma once

#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

struct Edge {
    int to;
    double weight;
};

class Graph {
public:
    Graph(int n, bool directed);

    void addEdge(int u, int v, double weight);
    void setCoords(int v, double x, double y);

    int numVertices() const;
    bool isDirected() const;
    bool hasCoords() const;

    const std::vector<Edge>& neighbors(int u) const;
    const std::vector<std::tuple<int, int, double>>& edgeList() const;
    std::pair<double, double> coords(int v) const;

    static Graph loadFromFile(const std::string& path);

private:
    int n_;
    bool directed_;
    bool hasCoords_ = false;
    std::vector<std::vector<Edge>> adj_;
    std::vector<std::tuple<int, int, double>> edges_;
    std::unordered_map<int, std::pair<double, double>> coords_;
};
