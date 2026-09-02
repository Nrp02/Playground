#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "../src/body.hpp"
#include "../src/nbody.hpp"
#include "../src/quadtree.hpp"
#include "../src/vec2.hpp"

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
    if (!(std::fabs(actual - expected) <= tolerance)) {
        std::cerr << "FAIL: " << testName << " (actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << ")\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

bool isFinite(const bh::Vec2& v) {
    return std::isfinite(v.x) && std::isfinite(v.y);
}

std::vector<bh::Body> fourCornerBodies() {
    std::vector<bh::Body> bodies;
    bodies.emplace_back(bh::Vec2(-1.0, -1.0), bh::Vec2(), 1.0);
    bodies.emplace_back(bh::Vec2(1.0, -1.0), bh::Vec2(), 2.0);
    bodies.emplace_back(bh::Vec2(-1.0, 1.0), bh::Vec2(), 3.0);
    bodies.emplace_back(bh::Vec2(1.0, 1.0), bh::Vec2(), 4.0);
    return bodies;
}

}

int main() {
    {
        std::vector<bh::Body> bodies;
        bh::BarnesHutTree tree;
        tree.build(bodies);
        expectTrue(tree.empty(), "tree over zero bodies is empty");
        expectNear(tree.totalMass(), 0.0, 0.0, "empty tree has zero total mass");

        bh::SimulationParams params;
        expectTrue(bh::barnesHutAccelerations(bodies, params).empty(),
                   "zero bodies yields zero accelerations");
        expectNear(bh::totalEnergy(bodies, params), 0.0, 0.0, "zero bodies has zero energy");

        bh::Simulator simulator(bodies, params);
        simulator.run(5);
        expectTrue(simulator.bodies().empty(), "simulating zero bodies stays empty");
    }

    {
        std::vector<bh::Body> bodies;
        bodies.emplace_back(bh::Vec2(3.0, -4.0), bh::Vec2(0.5, 0.25), 7.0);
        bh::BarnesHutTree tree;
        tree.build(bodies);
        expectNear(tree.totalMass(), 7.0, 1e-12, "single body root mass");
        expectNear(tree.centerOfMass().x, 3.0, 1e-12, "single body root com x");
        expectNear(tree.centerOfMass().y, -4.0, 1e-12, "single body root com y");

        bh::SimulationParams params;
        const bh::Vec2 acc = tree.acceleration(0, params);
        expectNear(bh::length(acc), 0.0, 0.0, "single body feels no self force");
        expectTrue(isFinite(acc), "single body acceleration is finite");
    }

    {
        const std::vector<bh::Body> bodies = fourCornerBodies();
        bh::BarnesHutTree tree;
        tree.build(bodies);

        expectNear(tree.totalMass(), 10.0, 1e-12, "root mass equals analytic total mass");
        expectNear(tree.centerOfMass().x, 0.2, 1e-12, "root center of mass x is analytic");
        expectNear(tree.centerOfMass().y, 0.4, 1e-12, "root center of mass y is analytic");
        expectNear(tree.root().quad.center.x, 0.0, 1e-12, "root quad is centered on the bounds");
        expectNear(tree.root().quad.center.y, 0.0, 1e-12, "root quad is centered vertically");
        expectTrue(tree.root().quad.halfSize >= 1.0, "root quad encloses every body");

        std::vector<int> leaves;
        bool quadrantsCorrect = true;
        bool leafMassesCorrect = true;
        for (int i = 0; i < 4; ++i) {
            const int leaf = tree.leafIndexFor(i);
            leaves.push_back(leaf);
            const bh::QuadNode& node = tree.node(leaf);
            const bh::Vec2& p = bodies[static_cast<std::size_t>(i)].position;
            if (!node.quad.contains(p)) {
                quadrantsCorrect = false;
            }
            if ((node.quad.center.x > 0.0) != (p.x > 0.0) ||
                (node.quad.center.y > 0.0) != (p.y > 0.0)) {
                quadrantsCorrect = false;
            }
            if (std::fabs(node.mass - bodies[static_cast<std::size_t>(i)].mass) > 1e-12) {
                leafMassesCorrect = false;
            }
        }
        expectTrue(quadrantsCorrect, "each body lands in the matching quadrant leaf");
        expectTrue(leafMassesCorrect, "each leaf carries exactly its body's mass");
        std::sort(leaves.begin(), leaves.end());
        expectTrue(std::unique(leaves.begin(), leaves.end()) == leaves.end(),
                   "four separated bodies occupy four distinct leaves");

        double childMassSum = 0.0;
        for (int i = 0; i < 4; ++i) {
            childMassSum += tree.node(tree.root().children[i]).mass;
        }
        expectNear(childMassSum, 10.0, 1e-12, "child masses sum to the root mass");
    }

    {
        std::vector<bh::Body> bodies =
            bh::makeUniformDisc(600, 30.0, 900.0, 1.0, 1234u);
        bh::SimulationParams params;
        params.softening = 0.05;
        params.theta = 0.0;

        const std::vector<bh::Vec2> exact = bh::bruteForceAccelerations(bodies, params);
        const std::vector<bh::Vec2> approximate = bh::barnesHutAccelerations(bodies, params);
        double worst = 0.0;
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            const double reference = bh::length(exact[i]);
            if (reference > 0.0) {
                worst = std::max(worst, bh::length(approximate[i] - exact[i]) / reference);
            }
        }
        expectTrue(worst < 1e-10, "theta = 0 reproduces brute force to tight tolerance");

        params.theta = 0.5;
        const double loose = bh::averageRelativeForceError(bodies, params);
        params.theta = 1.0;
        const double looser = bh::averageRelativeForceError(bodies, params);
        expectTrue(loose > 0.0 && loose < 5e-2, "theta = 0.5 stays accurate but approximate");
        expectTrue(looser > loose, "larger theta trades accuracy for speed");
    }

    {
        std::vector<bh::Body> bodies;
        bodies.emplace_back(bh::Vec2(-2.0, 1.0), bh::Vec2(), 3.0);
        bodies.emplace_back(bh::Vec2(4.0, -3.0), bh::Vec2(), 5.0);
        bh::SimulationParams params;
        params.theta = 0.0;
        params.softening = 1e-6;

        const std::vector<bh::Vec2> acc = bh::barnesHutAccelerations(bodies, params);
        const bh::Vec2 forceA = acc[0] * bodies[0].mass;
        const bh::Vec2 forceB = acc[1] * bodies[1].mass;
        expectNear(forceA.x, -forceB.x, 1e-15, "pairwise force x is equal and opposite");
        expectNear(forceA.y, -forceB.y, 1e-15, "pairwise force y is equal and opposite");

        const double separation = bh::length(bodies[1].position - bodies[0].position);
        const double expectedMagnitude = 3.0 * 5.0 / (separation * separation);
        expectNear(bh::length(forceA), expectedMagnitude, 1e-9,
                   "pairwise force magnitude matches Newton's law");
    }

    {
        std::vector<bh::Body> bodies;
        bodies.emplace_back(bh::Vec2(1.0, 1.0), bh::Vec2(), 2.0);
        bodies.emplace_back(bh::Vec2(1.0, 1.0), bh::Vec2(), 2.0);
        bh::SimulationParams params;
        params.softening = 1e-3;

        bh::BarnesHutTree tree;
        tree.build(bodies);
        expectNear(tree.totalMass(), 4.0, 1e-12, "coincident bodies still accumulate mass");

        const std::vector<bh::Vec2> acc = bh::barnesHutAccelerations(bodies, params);
        expectTrue(isFinite(acc[0]) && isFinite(acc[1]),
                   "coincident bodies produce finite accelerations");

        bodies[1].position = bh::Vec2(1.0 + 1e-14, 1.0 - 1e-14);
        const std::vector<bh::Vec2> nearAcc = bh::barnesHutAccelerations(bodies, params);
        expectTrue(isFinite(nearAcc[0]) && isFinite(nearAcc[1]),
                   "near-coincident bodies produce finite accelerations");

        bh::Simulator simulator(bodies, params);
        simulator.run(50);
        bool allFinite = true;
        for (const bh::Body& body : simulator.bodies()) {
            allFinite = allFinite && isFinite(body.position) && isFinite(body.velocity);
        }
        expectTrue(allFinite, "softening keeps a near-collision simulation free of NaN");
    }

    {
        bh::SimulationParams params;
        params.theta = 0.5;
        params.softening = 1e-6;
        params.timeStep = 1e-4;

        bh::Simulator simulator(bh::makeTwoBodyCircularOrbit(1.0, 1.0, 1.0, 1.0), params);
        const double period = 6.283185307179586 / std::sqrt(2.0);
        const int steps = static_cast<int>(3.0 * period / params.timeStep);

        double minSeparation = 1e30;
        double maxSeparation = 0.0;
        for (int i = 0; i < steps; ++i) {
            simulator.step();
            const double separation =
                bh::length(simulator.bodies()[1].position - simulator.bodies()[0].position);
            minSeparation = std::min(minSeparation, separation);
            maxSeparation = std::max(maxSeparation, separation);
        }
        expectTrue(maxSeparation - minSeparation < 1e-4,
                   "two-body circular orbit stays in a narrow radius band");
        expectNear(0.5 * (minSeparation + maxSeparation), 1.0, 1e-4,
                   "orbit radius matches the analytic separation");

        const bh::Vec2 barycenter =
            (simulator.bodies()[0].position + simulator.bodies()[1].position) * 0.5;
        expectNear(bh::length(barycenter), 0.0, 1e-9, "barycenter stays at the origin");
    }

    {
        bh::SimulationParams params;
        params.theta = 0.3;
        params.softening = 0.5;
        params.timeStep = 2e-3;

        std::vector<bh::Body> bodies = bh::makeUniformDisc(300, 25.0, 300.0, 1.0, 99u);
        bh::Simulator simulator(bodies, params);
        const double startEnergy = simulator.energy();
        simulator.run(3000);
        const double endEnergy = simulator.energy();
        expectTrue(std::isfinite(endEnergy), "energy stays finite over a long run");
        expectTrue(std::fabs((endEnergy - startEnergy) / startEnergy) < 1e-2,
                   "velocity-Verlet conserves energy within tolerance over 3000 steps");
    }

    {
        bh::SimulationParams params;
        params.softening = 0.5;
        params.timeStep = 1e-3;

        std::vector<bh::Body> bodies = bh::makeUniformDisc(120, 20.0, 200.0, 1.0, 5u);
        bh::Simulator simulator(bodies, params, bh::ForceMethod::BruteForce);
        const bh::Vec2 startMomentum = bh::totalMomentum(simulator.bodies());
        simulator.run(500);
        const bh::Vec2 endMomentum = bh::totalMomentum(simulator.bodies());
        expectNear(bh::length(endMomentum - startMomentum), 0.0, 1e-9,
                   "direct summation conserves total momentum");
    }

    {
        std::vector<bh::Body> bodies = fourCornerBodies();
        bh::BarnesHutTree tree;
        tree.build(bodies);
        const double oldHalfSize = tree.root().quad.halfSize;

        bodies[3].position = bh::Vec2(5000.0, -7000.0);
        expectTrue(!tree.root().quad.contains(bodies[3].position),
                   "moved body falls outside the stale root bounds");

        tree.build(bodies);
        expectTrue(tree.root().quad.halfSize > oldHalfSize, "rebuild grows the root bounds");
        bool allContained = true;
        for (const bh::Body& body : bodies) {
            allContained = allContained && tree.root().quad.contains(body.position);
        }
        expectTrue(allContained, "rebuilt tree encloses every relocated body");
        expectNear(tree.totalMass(), 10.0, 1e-12, "rebuilt tree preserves total mass");

        double comX = 0.0;
        double comY = 0.0;
        for (const bh::Body& body : bodies) {
            comX += body.position.x * body.mass;
            comY += body.position.y * body.mass;
        }
        expectNear(tree.centerOfMass().x, comX / 10.0, 1e-9, "rebuilt center of mass x is correct");
        expectNear(tree.centerOfMass().y, comY / 10.0, 1e-9, "rebuilt center of mass y is correct");

        bh::SimulationParams params;
        params.theta = 0.0;
        params.softening = 1e-6;
        const std::vector<bh::Vec2> exact = bh::bruteForceAccelerations(bodies, params);
        const std::vector<bh::Vec2> approximate = bh::barnesHutAccelerations(bodies, params);
        double worst = 0.0;
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            worst = std::max(worst, bh::length(approximate[i] - exact[i]) / bh::length(exact[i]));
        }
        expectTrue(worst < 1e-12, "forces after rebuild still match brute force at theta = 0");
    }

    {
        std::vector<bh::Body> bodies = bh::makeUniformDisc(400, 10.0, 400.0, 1.0, 77u);
        bh::BarnesHutTree tree;
        tree.build(bodies);
        expectEq<std::size_t>(bodies.size(), 400, "disc generator honours the requested count");
        expectTrue(tree.nodeCount() > bodies.size(),
                   "tree subdivides until leaves hold single bodies");
        expectNear(tree.totalMass(), 400.0, 1e-9, "disc generator distributes the total mass");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
