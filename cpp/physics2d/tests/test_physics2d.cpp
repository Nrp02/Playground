#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../src/world.hpp"

namespace {

int g_failures = 0;

void expectTrue(bool condition, const std::string& testName) {
    if (!condition) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

template <typename T>
void expectEq(const T& actual, const T& expected, const std::string& testName) {
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

void expectNear(double actual, double expected, double tolerance, const std::string& testName) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << testName << " (actual=" << actual << " expected=" << expected << ")\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

std::vector<Vec2> boxPoints(double halfWidth, double halfHeight) {
    return {Vec2(-halfWidth, -halfHeight), Vec2(halfWidth, -halfHeight), Vec2(halfWidth, halfHeight),
            Vec2(-halfWidth, halfHeight)};
}

}

int main() {
    {
        Body a(Vec2(0.0, 0.0), Shape::makeCircle(1.0), 1.0, false);
        Body b(Vec2(2.0, 0.0), Shape::makeCircle(1.0), 1.0, false);
        Manifold m;
        expectTrue(!collide(a, b, m), "exactly touching circles do not report a collision");
    }

    {
        Body a(Vec2(0.0, 0.0), Shape::makeCircle(1.0), 1.0, false);
        Body b(Vec2(1.5, 0.0), Shape::makeCircle(1.0), 1.0, false);
        Manifold m;
        expectTrue(collide(a, b, m), "deeply overlapping circles report a collision");
        expectNear(m.contacts[0].penetration, 0.5, 1e-9, "circle-circle deep overlap penetration depth");
        expectNear(m.normal.x, 1.0, 1e-9, "circle-circle manifold normal points from a to b (x)");
        expectNear(m.normal.y, 0.0, 1e-9, "circle-circle manifold normal points from a to b (y)");
    }

    {
        Body ground(Vec2(0.0, -0.5), Shape::makePolygon(boxPoints(5.0, 0.5)), 1.0, true);
        Body circle(Vec2(0.0, 0.3), Shape::makeCircle(0.5), 1.0, false);
        Manifold m;
        expectTrue(collide(circle, ground, m), "circle overlapping polygon top face collides");
        expectNear(m.contacts[0].penetration, 0.2, 1e-9, "circle-polygon penetration depth");
        expectNear(m.normal.x, 0.0, 1e-9, "circle-polygon manifold normal x (points from circle into polygon)");
        expectNear(m.normal.y, -1.0, 1e-9, "circle-polygon manifold normal y (points from circle into polygon)");
    }

    {
        Body ground(Vec2(0.0, -0.5), Shape::makePolygon(boxPoints(5.0, 0.5)), 1.0, true);
        Body circle(Vec2(0.0, 2.0), Shape::makeCircle(0.5), 1.0, false);
        Manifold m;
        expectTrue(!collide(circle, ground, m), "far-away circle does not collide with polygon");
    }

    {
        Body boxA(Vec2(0.0, 0.0), Shape::makePolygon(boxPoints(0.5, 0.5)), 1.0, false);
        Body boxB(Vec2(0.8, 0.0), Shape::makePolygon(boxPoints(0.5, 0.5)), 1.0, false);
        Manifold m;
        expectTrue(collide(boxA, boxB, m), "overlapping polygons collide");
        expectEq<std::size_t>(m.contacts.size(), 2, "polygon-polygon manifold has two clipped contact points");
        expectNear(m.normal.x, 1.0, 1e-6, "polygon-polygon manifold normal points from a to b (x)");
        expectNear(m.normal.y, 0.0, 1e-6, "polygon-polygon manifold normal points from a to b (y)");
        for (const auto& c : m.contacts) {
            expectNear(c.penetration, 0.2, 1e-6, "polygon-polygon penetration depth per contact");
        }
    }

    {
        Body boxA(Vec2(0.0, 0.0), Shape::makePolygon(boxPoints(0.5, 0.5)), 1.0, false);
        Body boxB(Vec2(1.0, 0.0), Shape::makePolygon(boxPoints(0.5, 0.5)), 1.0, false);
        Manifold m;
        expectTrue(!collide(boxA, boxB, m), "exactly touching polygons do not report a collision");
    }

    {
        std::vector<std::unique_ptr<Body>> storage;
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> posDist(-10.0, 10.0);
        std::uniform_real_distribution<double> radiusDist(0.2, 1.5);
        for (int i = 0; i < 120; ++i) {
            storage.push_back(std::make_unique<Body>(Vec2(posDist(rng), posDist(rng)),
                                                       Shape::makeCircle(radiusDist(rng)), 1.0, false));
        }
        std::vector<Body*> pointers;
        for (auto& b : storage) {
            pointers.push_back(b.get());
        }

        UniformGrid grid(1.5);
        std::vector<std::pair<int, int>> candidates = grid.computeCandidatePairs(pointers);

        std::vector<std::pair<int, int>> trueCollisions;
        for (std::size_t i = 0; i < pointers.size(); ++i) {
            for (std::size_t j = i + 1; j < pointers.size(); ++j) {
                Manifold m;
                if (collide(*pointers[i], *pointers[j], m)) {
                    trueCollisions.emplace_back(static_cast<int>(i), static_cast<int>(j));
                }
            }
        }

        bool allFound = true;
        for (const auto& pr : trueCollisions) {
            if (!std::binary_search(candidates.begin(), candidates.end(), pr)) {
                allFound = false;
                break;
            }
        }
        expectTrue(!trueCollisions.empty(), "randomized layout produced at least one true collision to check");
        expectTrue(allFound, "broad-phase candidate set is a superset of the true colliding pairs");
    }

    {
        World world(Vec2(0.0, -9.8));
        world.addPolygon(Vec2(0.0, -0.5), boxPoints(10.0, 0.5), 1.0, true);
        std::vector<Body*> boxes;
        int rows = 3;
        double size = 1.0;
        for (int row = 0; row < rows; ++row) {
            int count = rows - row;
            double y = 0.5 + row * (size + 0.02);
            double startX = -0.5 * (count - 1) * (size + 0.02);
            for (int i = 0; i < count; ++i) {
                double x = startX + i * (size + 0.02);
                boxes.push_back(world.addPolygon(Vec2(x, y), boxPoints(size * 0.5, size * 0.5), 1.0, false, 0.05, 0.5));
            }
        }

        double dt = 1.0 / 60.0;
        for (int i = 0; i < 500; ++i) {
            world.step(dt);
        }

        const auto& bodies = world.bodies();
        double maxPenetration = 0.0;
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            for (std::size_t j = i + 1; j < bodies.size(); ++j) {
                Body* a = bodies[i].get();
                Body* b = bodies[j].get();
                if (a->invMass == 0.0 && b->invMass == 0.0) {
                    continue;
                }
                Manifold m;
                if (collide(*a, *b, m)) {
                    for (const auto& c : m.contacts) {
                        maxPenetration = std::max(maxPenetration, c.penetration);
                    }
                }
            }
        }
        expectTrue(maxPenetration < 0.05, "no persistent significant penetration after a settling simulation");

        std::vector<Vec2> before;
        for (auto* b : boxes) {
            before.push_back(b->position);
        }
        for (int i = 0; i < 60; ++i) {
            world.step(dt);
        }
        double maxDrift = 0.0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            maxDrift = std::max(maxDrift, length(boxes[i]->position - before[i]));
        }
        expectTrue(maxDrift < 0.01, "a resting stack stays at rest across many additional steps");
    }

    {
        World world(Vec2(0.0, 0.0));
        Body* wall = world.addPolygon(Vec2(10.0, 0.0), boxPoints(0.05, 5.0), 1.0, true);
        Body* fastBall = world.addCircle(Vec2(0.0, 0.0), 0.2, 1.0, false, 0.3, 0.1);
        fastBall->linearVelocity = Vec2(500.0, 0.0);

        double dt = 1.0 / 60.0;
        for (int i = 0; i < 10; ++i) {
            world.step(dt);
        }
        expectTrue(fastBall->position.x < wall->position.x + 0.05, "fast body does not tunnel through a thin static wall");
    }

    {
        World world(Vec2(0.0, 0.0));
        Body* a = world.addCircle(Vec2(-2.0, 0.0), 0.5, 1.0, false, 1.0, 0.0);
        Body* b = world.addCircle(Vec2(2.0, 0.0), 0.3, 1.0, false, 1.0, 0.0);
        a->linearVelocity = Vec2(5.0, 0.0);
        b->linearVelocity = Vec2(0.0, 0.0);

        double massA = 1.0 / a->invMass;
        double massB = 1.0 / b->invMass;
        double momentumBefore = massA * a->linearVelocity.x + massB * b->linearVelocity.x;
        double energyBefore = world.totalKineticEnergy();

        double dt = 1.0 / 240.0;
        for (int i = 0; i < 480; ++i) {
            world.step(dt);
        }

        double momentumAfter = massA * a->linearVelocity.x + massB * b->linearVelocity.x;
        expectNear(momentumAfter, momentumBefore, 1e-6 * std::fabs(momentumBefore) + 1e-6,
                   "momentum is conserved in a head-on elastic collision");
        double energyAfter = world.totalKineticEnergy();
        expectTrue(energyAfter <= energyBefore + 1e-6, "energy does not increase in a perfectly elastic collision");
    }

    {
        World world(Vec2(0.0, 0.0));
        Body* a = world.addCircle(Vec2(-2.0, 0.0), 0.5, 1.0, false, 0.5, 0.0);
        Body* b = world.addCircle(Vec2(2.0, 0.0), 0.5, 1.0, false, 0.5, 0.0);
        a->linearVelocity = Vec2(5.0, 0.0);
        b->linearVelocity = Vec2(-5.0, 0.0);

        double energyBefore = world.totalKineticEnergy();
        double dt = 1.0 / 240.0;
        for (int i = 0; i < 480; ++i) {
            world.step(dt);
        }
        double energyAfter = world.totalKineticEnergy();
        expectTrue(energyAfter < energyBefore - 1e-6, "energy strictly decreases with restitution below one");
    }

    {
        auto buildWorld = []() {
            World world(Vec2(0.0, -9.8));
            world.addPolygon(Vec2(0.0, -0.5), boxPoints(10.0, 0.5), 1.0, true);
            world.addCircle(Vec2(0.0, 5.0), 0.5, 1.0, false, 0.6, 0.3);
            world.addPolygon(Vec2(1.3, 6.0), boxPoints(0.5, 0.5), 1.0, false, 0.3, 0.4);
            world.addCircle(Vec2(-1.1, 4.0), 0.4, 1.0, false, 0.5, 0.2);
            return world;
        };

        World w1 = buildWorld();
        World w2 = buildWorld();
        double dt = 1.0 / 120.0;
        for (int i = 0; i < 300; ++i) {
            w1.step(dt);
            w2.step(dt);
        }

        bool identical = true;
        for (std::size_t i = 0; i < w1.bodies().size(); ++i) {
            const Body* b1 = w1.bodies()[i].get();
            const Body* b2 = w2.bodies()[i].get();
            if (b1->position.x != b2->position.x || b1->position.y != b2->position.y ||
                b1->linearVelocity.x != b2->linearVelocity.x || b1->linearVelocity.y != b2->linearVelocity.y ||
                b1->angle != b2->angle || b1->angularVelocity != b2->angularVelocity) {
                identical = false;
                break;
            }
        }
        expectTrue(identical, "stepping the same initial world twice yields bit-identical states");
    }

    std::cout << (g_failures == 0 ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
