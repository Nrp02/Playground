#ifndef BARNES_HUT_NBODY_BODY_HPP
#define BARNES_HUT_NBODY_BODY_HPP

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include "vec2.hpp"

namespace bh {

struct Body {
    Vec2 position;
    Vec2 velocity;
    double mass = 1.0;

    Body() = default;
    Body(const Vec2& p, const Vec2& v, double m) : position(p), velocity(v), mass(m) {}
};

struct SimulationParams {
    double gravitationalConstant = 1.0;
    double softening = 1e-3;
    double theta = 0.5;
    double timeStep = 1e-3;
};

inline std::vector<Body> makeUniformDisc(std::size_t count, double radius, double totalMass,
                                         double gravitationalConstant, unsigned seed) {
    std::vector<Body> bodies;
    bodies.reserve(count);
    if (count == 0) {
        return bodies;
    }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> angleDist(0.0, 6.283185307179586);
    std::uniform_real_distribution<double> radialDist(0.0, 1.0);

    const double perBodyMass = totalMass / static_cast<double>(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double angle = angleDist(rng);
        const double r = radius * std::sqrt(radialDist(rng));
        const Vec2 position(r * std::cos(angle), r * std::sin(angle));

        const double enclosedFraction = (radius > 0.0) ? (r * r) / (radius * radius) : 0.0;
        const double enclosedMass = totalMass * enclosedFraction;
        double speed = 0.0;
        if (r > 1e-9) {
            speed = std::sqrt(gravitationalConstant * enclosedMass / r);
        }
        const Vec2 velocity(-speed * std::sin(angle), speed * std::cos(angle));
        bodies.emplace_back(position, velocity, perBodyMass);
    }
    return bodies;
}

inline std::vector<Body> makeTwoBodyCircularOrbit(double massA, double massB, double separation,
                                                  double gravitationalConstant) {
    const double totalMass = massA + massB;
    const double radiusA = separation * massB / totalMass;
    const double radiusB = separation * massA / totalMass;
    const double relativeSpeed = std::sqrt(gravitationalConstant * totalMass / separation);
    const double speedA = relativeSpeed * massB / totalMass;
    const double speedB = relativeSpeed * massA / totalMass;

    std::vector<Body> bodies;
    bodies.reserve(2);
    bodies.emplace_back(Vec2(-radiusA, 0.0), Vec2(0.0, -speedA), massA);
    bodies.emplace_back(Vec2(radiusB, 0.0), Vec2(0.0, speedB), massB);
    return bodies;
}

}

#endif
