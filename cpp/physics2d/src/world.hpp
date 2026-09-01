#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "broadphase.hpp"
#include "collision.hpp"

class World {
public:
    explicit World(const Vec2& gravity = Vec2(0.0, -9.8), double gridCellSize = 4.0);

    Body* addCircle(const Vec2& position, double radius, double density, bool isStatic, double restitution = 0.2,
                     double friction = 0.3);
    Body* addPolygon(const Vec2& position, std::vector<Vec2> localPoints, double density, bool isStatic,
                      double restitution = 0.2, double friction = 0.3);

    void step(double dt);

    const std::vector<std::unique_ptr<Body>>& bodies() const { return bodies_; }
    std::vector<Body*> bodyPointers() const;

    double totalKineticEnergy() const;

    const std::vector<std::pair<int, int>>& lastCandidatePairs() const { return lastCandidatePairs_; }
    const std::vector<Manifold>& lastManifolds() const { return lastManifolds_; }

    int velocityIterations = 8;
    double positionCorrectionPercent = 0.2;
    double penetrationSlop = 0.005;

private:
    void substep(double dt);
    double minConservativeRadius() const;

    std::vector<std::unique_ptr<Body>> bodies_;
    Vec2 gravity_;
    UniformGrid grid_;
    std::vector<std::pair<int, int>> lastCandidatePairs_;
    std::vector<Manifold> lastManifolds_;
};
