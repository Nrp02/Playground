#ifndef BARNES_HUT_NBODY_NBODY_HPP
#define BARNES_HUT_NBODY_NBODY_HPP

#include <cmath>
#include <cstddef>
#include <vector>

#include "body.hpp"
#include "quadtree.hpp"
#include "vec2.hpp"

namespace bh {

inline std::vector<Vec2> bruteForceAccelerations(const std::vector<Body>& bodies,
                                                 const SimulationParams& params) {
    const std::size_t n = bodies.size();
    std::vector<Vec2> acc(n);
    const double eps2 = params.softening * params.softening;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const Vec2 delta = bodies[j].position - bodies[i].position;
            const double softened = lengthSquared(delta) + eps2;
            if (softened <= 0.0) {
                continue;
            }
            const double inverse = 1.0 / std::sqrt(softened);
            const double inverseCubed = inverse * inverse * inverse;
            const double base = params.gravitationalConstant * inverseCubed;
            acc[i] += delta * (base * bodies[j].mass);
            acc[j] += delta * (-base * bodies[i].mass);
        }
    }
    return acc;
}

inline std::vector<Vec2> barnesHutAccelerations(const std::vector<Body>& bodies,
                                                const SimulationParams& params) {
    std::vector<Vec2> acc(bodies.size());
    if (bodies.empty()) {
        return acc;
    }
    BarnesHutTree tree;
    tree.build(bodies);
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        acc[i] = tree.acceleration(static_cast<int>(i), params);
    }
    return acc;
}

inline double kineticEnergy(const std::vector<Body>& bodies) {
    double total = 0.0;
    for (const Body& body : bodies) {
        total += 0.5 * body.mass * lengthSquared(body.velocity);
    }
    return total;
}

inline double potentialEnergy(const std::vector<Body>& bodies, const SimulationParams& params) {
    double total = 0.0;
    const double eps2 = params.softening * params.softening;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        for (std::size_t j = i + 1; j < bodies.size(); ++j) {
            const Vec2 delta = bodies[j].position - bodies[i].position;
            const double distance = std::sqrt(lengthSquared(delta) + eps2);
            total -= params.gravitationalConstant * bodies[i].mass * bodies[j].mass / distance;
        }
    }
    return total;
}

inline double totalEnergy(const std::vector<Body>& bodies, const SimulationParams& params) {
    return kineticEnergy(bodies) + potentialEnergy(bodies, params);
}

inline Vec2 totalMomentum(const std::vector<Body>& bodies) {
    Vec2 total;
    for (const Body& body : bodies) {
        total += body.velocity * body.mass;
    }
    return total;
}

enum class ForceMethod { BarnesHut, BruteForce };

class Simulator {
public:
    Simulator(std::vector<Body> bodies, const SimulationParams& params,
              ForceMethod method = ForceMethod::BarnesHut)
        : bodies_(std::move(bodies)), params_(params), method_(method) {
        accelerations_ = computeAccelerations();
    }

    void step() {
        const double dt = params_.timeStep;
        const double halfDt = 0.5 * dt;
        for (std::size_t i = 0; i < bodies_.size(); ++i) {
            bodies_[i].velocity += accelerations_[i] * halfDt;
            bodies_[i].position += bodies_[i].velocity * dt;
        }
        accelerations_ = computeAccelerations();
        for (std::size_t i = 0; i < bodies_.size(); ++i) {
            bodies_[i].velocity += accelerations_[i] * halfDt;
        }
    }

    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            step();
        }
    }

    const std::vector<Body>& bodies() const { return bodies_; }

    const std::vector<Vec2>& accelerations() const { return accelerations_; }

    double energy() const { return totalEnergy(bodies_, params_); }

    const SimulationParams& params() const { return params_; }

private:
    std::vector<Vec2> computeAccelerations() const {
        if (method_ == ForceMethod::BruteForce) {
            return bruteForceAccelerations(bodies_, params_);
        }
        return barnesHutAccelerations(bodies_, params_);
    }

    std::vector<Body> bodies_;
    SimulationParams params_;
    ForceMethod method_;
    std::vector<Vec2> accelerations_;
};

inline double averageRelativeForceError(const std::vector<Body>& bodies,
                                        const SimulationParams& params) {
    const std::vector<Vec2> exact = bruteForceAccelerations(bodies, params);
    const std::vector<Vec2> approximate = barnesHutAccelerations(bodies, params);
    double total = 0.0;
    std::size_t counted = 0;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const double reference = length(exact[i]);
        if (reference <= 0.0) {
            continue;
        }
        total += length(approximate[i] - exact[i]) / reference;
        ++counted;
    }
    return (counted == 0) ? 0.0 : total / static_cast<double>(counted);
}

}

#endif
