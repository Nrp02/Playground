#ifndef BARNES_HUT_NBODY_QUADTREE_HPP
#define BARNES_HUT_NBODY_QUADTREE_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "body.hpp"
#include "vec2.hpp"

namespace bh {

struct Quad {
    Vec2 center;
    double halfSize = 0.0;

    bool contains(const Vec2& p) const {
        return p.x >= center.x - halfSize && p.x <= center.x + halfSize &&
               p.y >= center.y - halfSize && p.y <= center.y + halfSize;
    }
};

struct QuadNode {
    Quad quad;
    double mass = 0.0;
    Vec2 centerOfMass;
    int firstBody = -1;
    int children[4] = {-1, -1, -1, -1};

    bool isLeaf() const { return children[0] < 0; }
};

class BarnesHutTree {
public:
    static constexpr int kMaxDepth = 48;

    void build(const std::vector<Body>& bodies) {
        bodies_ = &bodies;
        nodes_.clear();
        nextBody_.assign(bodies.size(), -1);
        if (bodies.empty()) {
            return;
        }

        nodes_.reserve(bodies.size() * 3 + 8);
        nodes_.push_back(QuadNode());
        nodes_[0].quad = boundingQuad(bodies);

        for (std::size_t i = 0; i < bodies.size(); ++i) {
            insertBody(0, static_cast<int>(i), 0);
        }
        computeMassDistribution(0);
    }

    bool empty() const { return nodes_.empty(); }

    std::size_t nodeCount() const { return nodes_.size(); }

    const QuadNode& root() const { return nodes_[0]; }

    const QuadNode& node(int index) const { return nodes_[static_cast<std::size_t>(index)]; }

    double totalMass() const { return nodes_.empty() ? 0.0 : nodes_[0].mass; }

    Vec2 centerOfMass() const { return nodes_.empty() ? Vec2() : nodes_[0].centerOfMass; }

    int leafIndexFor(int bodyIndex) const {
        if (nodes_.empty()) {
            return -1;
        }
        int current = 0;
        while (!nodes_[static_cast<std::size_t>(current)].isLeaf()) {
            current = childIndexFor(current, bodyIndex);
        }
        return current;
    }

    Vec2 acceleration(int bodyIndex, const SimulationParams& params) const {
        Vec2 acc;
        if (nodes_.empty()) {
            return acc;
        }
        accumulate(0, bodyIndex, params, acc);
        return acc;
    }

    static Quad boundingQuad(const std::vector<Body>& bodies) {
        Quad quad;
        if (bodies.empty()) {
            return quad;
        }
        double minX = std::numeric_limits<double>::max();
        double minY = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double maxY = std::numeric_limits<double>::lowest();
        for (const Body& body : bodies) {
            minX = std::min(minX, body.position.x);
            maxX = std::max(maxX, body.position.x);
            minY = std::min(minY, body.position.y);
            maxY = std::max(maxY, body.position.y);
        }
        quad.center = Vec2(0.5 * (minX + maxX), 0.5 * (minY + maxY));
        const double extent = std::max(maxX - minX, maxY - minY);
        quad.halfSize = 0.5 * extent * 1.0001 + 1e-9;
        return quad;
    }

private:
    static int quadrantOf(const Quad& quad, const Vec2& p) {
        const int right = (p.x >= quad.center.x) ? 1 : 0;
        const int top = (p.y >= quad.center.y) ? 2 : 0;
        return right + top;
    }

    static Quad childQuad(const Quad& quad, int index) {
        const double h = quad.halfSize * 0.5;
        const double dx = ((index & 1) != 0) ? h : -h;
        const double dy = ((index & 2) != 0) ? h : -h;
        Quad child;
        child.center = Vec2(quad.center.x + dx, quad.center.y + dy);
        child.halfSize = h;
        return child;
    }

    int childIndexFor(int nodeIndex, int bodyIndex) const {
        const std::size_t n = static_cast<std::size_t>(nodeIndex);
        const int q = quadrantOf(nodes_[n].quad, (*bodies_)[static_cast<std::size_t>(bodyIndex)].position);
        return nodes_[n].children[q];
    }

    void subdivide(int nodeIndex) {
        const std::size_t n = static_cast<std::size_t>(nodeIndex);
        const Quad parentQuad = nodes_[n].quad;
        const int base = static_cast<int>(nodes_.size());
        for (int i = 0; i < 4; ++i) {
            QuadNode child;
            child.quad = childQuad(parentQuad, i);
            nodes_.push_back(child);
        }
        for (int i = 0; i < 4; ++i) {
            nodes_[n].children[i] = base + i;
        }
    }

    void insertBody(int nodeIndex, int bodyIndex, int depth) {
        const std::size_t n = static_cast<std::size_t>(nodeIndex);
        if (nodes_[n].isLeaf()) {
            if (nodes_[n].firstBody < 0) {
                nodes_[n].firstBody = bodyIndex;
                nextBody_[static_cast<std::size_t>(bodyIndex)] = -1;
                return;
            }
            if (depth >= kMaxDepth) {
                nextBody_[static_cast<std::size_t>(bodyIndex)] = nodes_[n].firstBody;
                nodes_[n].firstBody = bodyIndex;
                return;
            }
            int existing = nodes_[n].firstBody;
            nodes_[n].firstBody = -1;
            subdivide(nodeIndex);
            while (existing >= 0) {
                const int following = nextBody_[static_cast<std::size_t>(existing)];
                insertBody(childIndexFor(nodeIndex, existing), existing, depth + 1);
                existing = following;
            }
        }
        insertBody(childIndexFor(nodeIndex, bodyIndex), bodyIndex, depth + 1);
    }

    void computeMassDistribution(int nodeIndex) {
        const std::size_t n = static_cast<std::size_t>(nodeIndex);
        double mass = 0.0;
        Vec2 weighted;
        if (nodes_[n].isLeaf()) {
            for (int b = nodes_[n].firstBody; b >= 0; b = nextBody_[static_cast<std::size_t>(b)]) {
                const Body& body = (*bodies_)[static_cast<std::size_t>(b)];
                mass += body.mass;
                weighted += body.position * body.mass;
            }
        } else {
            for (int i = 0; i < 4; ++i) {
                const int childIndex = nodes_[n].children[i];
                computeMassDistribution(childIndex);
                const QuadNode& child = nodes_[static_cast<std::size_t>(childIndex)];
                mass += child.mass;
                weighted += child.centerOfMass * child.mass;
            }
        }
        nodes_[n].mass = mass;
        nodes_[n].centerOfMass = (mass > 0.0) ? weighted * (1.0 / mass) : nodes_[n].quad.center;
    }

    void accumulate(int nodeIndex, int bodyIndex, const SimulationParams& params, Vec2& acc) const {
        const std::size_t n = static_cast<std::size_t>(nodeIndex);
        const QuadNode& current = nodes_[n];
        if (current.mass <= 0.0) {
            return;
        }
        const Vec2& target = (*bodies_)[static_cast<std::size_t>(bodyIndex)].position;

        if (current.isLeaf()) {
            for (int b = current.firstBody; b >= 0; b = nextBody_[static_cast<std::size_t>(b)]) {
                if (b == bodyIndex) {
                    continue;
                }
                const Body& other = (*bodies_)[static_cast<std::size_t>(b)];
                addPointMass(target, other.position, other.mass, params, acc);
            }
            return;
        }

        const Vec2 delta = current.centerOfMass - target;
        const double distance = std::sqrt(lengthSquared(delta) + params.softening * params.softening);
        const double width = 2.0 * current.quad.halfSize;
        if (distance > 0.0 && width < params.theta * distance) {
            addPointMass(target, current.centerOfMass, current.mass, params, acc);
            return;
        }
        for (int i = 0; i < 4; ++i) {
            accumulate(current.children[i], bodyIndex, params, acc);
        }
    }

    static void addPointMass(const Vec2& target, const Vec2& source, double mass,
                             const SimulationParams& params, Vec2& acc) {
        const Vec2 delta = source - target;
        const double softened = lengthSquared(delta) + params.softening * params.softening;
        if (softened <= 0.0) {
            return;
        }
        const double inverse = 1.0 / std::sqrt(softened);
        const double scale = params.gravitationalConstant * mass * inverse * inverse * inverse;
        acc += delta * scale;
    }

    const std::vector<Body>* bodies_ = nullptr;
    std::vector<QuadNode> nodes_;
    std::vector<int> nextBody_;
};

}

#endif
