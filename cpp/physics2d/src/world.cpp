#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

void resolveVelocity(Manifold& m) {
    Body& a = *m.a;
    Body& b = *m.b;
    double restitution = std::min(a.restitution, b.restitution);
    double friction = std::sqrt(a.friction * b.friction);

    for (auto& c : m.contacts) {
        Vec2 r1 = c.point - a.position;
        Vec2 r2 = c.point - b.position;
        Vec2 rv = (b.linearVelocity + cross(b.angularVelocity, r2)) - (a.linearVelocity + cross(a.angularVelocity, r1));
        double velAlongNormal = dot(rv, m.normal);
        if (velAlongNormal > 0.0) {
            continue;
        }

        double rn1 = cross(r1, m.normal);
        double rn2 = cross(r2, m.normal);
        double invMassSum = a.invMass + b.invMass + a.invInertia * rn1 * rn1 + b.invInertia * rn2 * rn2;
        if (invMassSum <= 0.0) {
            continue;
        }

        double j = -(1.0 + restitution) * velAlongNormal / invMassSum;
        Vec2 impulse = m.normal * j;
        a.linearVelocity -= impulse * a.invMass;
        b.linearVelocity += impulse * b.invMass;
        a.angularVelocity -= a.invInertia * cross(r1, impulse);
        b.angularVelocity += b.invInertia * cross(r2, impulse);

        rv = (b.linearVelocity + cross(b.angularVelocity, r2)) - (a.linearVelocity + cross(a.angularVelocity, r1));
        Vec2 tangentRaw = rv - m.normal * dot(rv, m.normal);
        double tangentLen = length(tangentRaw);
        if (tangentLen > 1e-9) {
            Vec2 tangent = tangentRaw * (1.0 / tangentLen);
            double rt1 = cross(r1, tangent);
            double rt2 = cross(r2, tangent);
            double invMassSumT = a.invMass + b.invMass + a.invInertia * rt1 * rt1 + b.invInertia * rt2 * rt2;
            if (invMassSumT > 0.0) {
                double jt = -dot(rv, tangent) / invMassSumT;
                double maxFriction = friction * std::fabs(j);
                jt = std::max(-maxFriction, std::min(maxFriction, jt));
                Vec2 frictionImpulse = tangent * jt;
                a.linearVelocity -= frictionImpulse * a.invMass;
                b.linearVelocity += frictionImpulse * b.invMass;
                a.angularVelocity -= a.invInertia * cross(r1, frictionImpulse);
                b.angularVelocity += b.invInertia * cross(r2, frictionImpulse);
            }
        }
    }
}

void positionalCorrection(Manifold& m, double percent, double slop) {
    Body& a = *m.a;
    Body& b = *m.b;
    double invMassSum = a.invMass + b.invMass;
    if (invMassSum <= 0.0) {
        return;
    }
    for (auto& c : m.contacts) {
        double correctionMag = std::max(c.penetration - slop, 0.0) / invMassSum * percent;
        Vec2 correction = m.normal * correctionMag;
        a.position -= correction * a.invMass;
        b.position += correction * b.invMass;
    }
}

}

World::World(const Vec2& gravity, double gridCellSize) : gravity_(gravity), grid_(gridCellSize) {}

Body* World::addCircle(const Vec2& position, double radius, double density, bool isStatic, double restitution,
                        double friction) {
    auto body = std::make_unique<Body>(position, Shape::makeCircle(radius), density, isStatic);
    body->restitution = restitution;
    body->friction = friction;
    Body* raw = body.get();
    bodies_.push_back(std::move(body));
    return raw;
}

Body* World::addPolygon(const Vec2& position, std::vector<Vec2> localPoints, double density, bool isStatic,
                         double restitution, double friction) {
    auto body = std::make_unique<Body>(position, Shape::makePolygon(std::move(localPoints)), density, isStatic);
    body->restitution = restitution;
    body->friction = friction;
    Body* raw = body.get();
    bodies_.push_back(std::move(body));
    return raw;
}

std::vector<Body*> World::bodyPointers() const {
    std::vector<Body*> result;
    result.reserve(bodies_.size());
    for (const auto& b : bodies_) {
        result.push_back(b.get());
    }
    return result;
}

double World::totalKineticEnergy() const {
    double energy = 0.0;
    for (const auto& b : bodies_) {
        if (b->invMass == 0.0) {
            continue;
        }
        double mass = 1.0 / b->invMass;
        double inertia = b->invInertia > 0.0 ? 1.0 / b->invInertia : 0.0;
        energy += 0.5 * mass * lengthSquared(b->linearVelocity);
        energy += 0.5 * inertia * b->angularVelocity * b->angularVelocity;
    }
    return energy;
}

double World::minConservativeRadius() const {
    if (bodies_.empty()) {
        return std::numeric_limits<double>::max();
    }
    double minVal = std::numeric_limits<double>::max();
    for (const auto& b : bodies_) {
        minVal = std::min(minVal, b->shape.conservativeRadius());
    }
    return minVal;
}

void World::step(double dt) {
    double minFeature = minConservativeRadius();
    double maxTravel = 0.0;
    for (const auto& b : bodies_) {
        if (b->invMass == 0.0) {
            continue;
        }
        Vec2 approxVel = b->linearVelocity + gravity_ * dt;
        maxTravel = std::max(maxTravel, length(approxVel) * dt);
    }

    int substeps = 1;
    if (minFeature < std::numeric_limits<double>::max() && minFeature > 1e-9 && maxTravel > minFeature) {
        substeps = static_cast<int>(std::ceil(maxTravel / minFeature));
    }
    substeps = std::max(1, std::min(substeps, 64));
    double subDt = dt / static_cast<double>(substeps);
    for (int i = 0; i < substeps; ++i) {
        substep(subDt);
    }
}

void World::substep(double dt) {
    for (auto& bptr : bodies_) {
        Body& b = *bptr;
        if (b.invMass == 0.0) {
            continue;
        }
        b.linearVelocity += gravity_ * dt;
    }

    std::vector<Body*> pointers = bodyPointers();
    lastCandidatePairs_ = grid_.computeCandidatePairs(pointers);

    std::vector<Manifold> manifolds;
    manifolds.reserve(lastCandidatePairs_.size());
    for (const auto& pr : lastCandidatePairs_) {
        Body& a = *bodies_[static_cast<std::size_t>(pr.first)];
        Body& b = *bodies_[static_cast<std::size_t>(pr.second)];
        if (a.invMass == 0.0 && b.invMass == 0.0) {
            continue;
        }
        Manifold m;
        if (collide(a, b, m)) {
            manifolds.push_back(std::move(m));
        }
    }

    for (int iter = 0; iter < velocityIterations; ++iter) {
        for (auto& m : manifolds) {
            resolveVelocity(m);
        }
    }

    for (auto& bptr : bodies_) {
        Body& b = *bptr;
        if (b.invMass == 0.0) {
            continue;
        }
        b.position += b.linearVelocity * dt;
        b.angle += b.angularVelocity * dt;
    }

    for (auto& m : manifolds) {
        positionalCorrection(m, positionCorrectionPercent, penetrationSlop);
    }

    lastManifolds_ = std::move(manifolds);
}
