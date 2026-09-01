#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "shapes.hpp"

struct AABB {
    Vec2 min;
    Vec2 max;
};

inline AABB computeAABB(const Body& b) {
    if (b.shape.type() == ShapeType::Circle) {
        double r = b.shape.radius();
        return AABB{Vec2(b.position.x - r, b.position.y - r), Vec2(b.position.x + r, b.position.y + r)};
    }
    const auto& verts = b.shape.localVertices();
    Vec2 mn(std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    Vec2 mx(-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max());
    for (std::size_t i = 0; i < verts.size(); ++i) {
        Vec2 w = b.worldVertex(i);
        mn.x = std::min(mn.x, w.x);
        mn.y = std::min(mn.y, w.y);
        mx.x = std::max(mx.x, w.x);
        mx.y = std::max(mx.y, w.y);
    }
    return AABB{mn, mx};
}

inline bool aabbOverlap(const AABB& a, const AABB& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y;
}

class UniformGrid {
public:
    explicit UniformGrid(double cellSize) : cellSize_(cellSize) {}

    std::vector<std::pair<int, int>> computeCandidatePairs(const std::vector<Body*>& bodies) const {
        std::unordered_map<long long, std::vector<int>> cells;
        std::vector<AABB> boxes(bodies.size());
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            boxes[i] = computeAABB(*bodies[i]);
            long long x0 = cellCoord(boxes[i].min.x);
            long long x1 = cellCoord(boxes[i].max.x);
            long long y0 = cellCoord(boxes[i].min.y);
            long long y1 = cellCoord(boxes[i].max.y);
            for (long long cx = x0; cx <= x1; ++cx) {
                for (long long cy = y0; cy <= y1; ++cy) {
                    cells[cellKey(cx, cy)].push_back(static_cast<int>(i));
                }
            }
        }

        std::vector<std::pair<int, int>> pairs;
        for (const auto& entry : cells) {
            const auto& ids = entry.second;
            for (std::size_t i = 0; i < ids.size(); ++i) {
                for (std::size_t j = i + 1; j < ids.size(); ++j) {
                    int a = std::min(ids[i], ids[j]);
                    int b = std::max(ids[i], ids[j]);
                    if (aabbOverlap(boxes[static_cast<std::size_t>(a)], boxes[static_cast<std::size_t>(b)])) {
                        pairs.emplace_back(a, b);
                    }
                }
            }
        }
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
        return pairs;
    }

    static std::vector<std::pair<int, int>> bruteForcePairs(const std::vector<Body*>& bodies) {
        std::vector<AABB> boxes(bodies.size());
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            boxes[i] = computeAABB(*bodies[i]);
        }
        std::vector<std::pair<int, int>> pairs;
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            for (std::size_t j = i + 1; j < bodies.size(); ++j) {
                if (aabbOverlap(boxes[i], boxes[j])) {
                    pairs.emplace_back(static_cast<int>(i), static_cast<int>(j));
                }
            }
        }
        return pairs;
    }

private:
    double cellSize_;

    long long cellCoord(double v) const {
        return static_cast<long long>(std::floor(v / cellSize_));
    }

    static long long cellKey(long long cx, long long cy) {
        return (cx * 73856093LL) ^ (cy * 19349663LL);
    }
};
