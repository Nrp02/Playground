#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

#include "body.hpp"
#include "nbody.hpp"
#include "quadtree.hpp"
#include "vec2.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(const Clock::time_point& start) {
    const auto elapsed = Clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

void printScalingComparison() {
    std::cout << "=== one force step: Barnes-Hut vs brute force ===\n";
    std::cout << std::left << std::setw(9) << "N" << std::setw(16) << "barnes-hut(ms)"
              << std::setw(16) << "brute(ms)" << std::setw(12) << "speedup" << std::setw(12)
              << "tree nodes" << std::setw(14) << "avg rel err" << "\n";

    bh::SimulationParams params;
    params.theta = 0.5;
    params.softening = 1e-2;

    for (std::size_t n : {1000u, 2000u, 4000u, 8000u, 16000u, 32000u}) {
        const std::vector<bh::Body> bodies =
            bh::makeUniformDisc(n, 100.0, 1000.0, params.gravitationalConstant, 7u);

        bh::BarnesHutTree tree;
        const auto treeStart = Clock::now();
        tree.build(bodies);
        std::vector<bh::Vec2> approximate(bodies.size());
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            approximate[i] = tree.acceleration(static_cast<int>(i), params);
        }
        const double treeMs = millisecondsSince(treeStart);

        const auto bruteStart = Clock::now();
        const std::vector<bh::Vec2> exact = bh::bruteForceAccelerations(bodies, params);
        const double bruteMs = millisecondsSince(bruteStart);

        double errorSum = 0.0;
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            const double reference = bh::length(exact[i]);
            if (reference > 0.0) {
                errorSum += bh::length(approximate[i] - exact[i]) / reference;
            }
        }

        std::cout << std::left << std::setw(9) << n << std::setw(16) << std::fixed
                  << std::setprecision(2) << treeMs << std::setw(16) << bruteMs << std::setw(12)
                  << std::setprecision(1) << (bruteMs / treeMs) << std::setw(12)
                  << tree.nodeCount() << std::setw(14) << std::scientific << std::setprecision(2)
                  << (errorSum / static_cast<double>(bodies.size())) << "\n";
    }
}

void printAccuracySweep() {
    std::cout << "\n=== accuracy vs theta (N = 3000 uniform disc) ===\n";
    bh::SimulationParams params;
    params.softening = 1e-2;
    const std::vector<bh::Body> bodies =
        bh::makeUniformDisc(3000, 100.0, 1000.0, params.gravitationalConstant, 11u);

    std::cout << std::left << std::setw(10) << "theta" << std::setw(22) << "avg rel force error"
              << std::setw(12) << "time(ms)" << "\n";
    for (double theta : {0.0, 0.2, 0.5, 0.8, 1.0}) {
        params.theta = theta;
        const auto start = Clock::now();
        const std::vector<bh::Vec2> approximate = bh::barnesHutAccelerations(bodies, params);
        const double ms = millisecondsSince(start);
        double checksum = 0.0;
        for (const bh::Vec2& a : approximate) {
            checksum += bh::length(a);
        }
        const double error = bh::averageRelativeForceError(bodies, params);
        std::cout << std::left << std::setw(10) << std::fixed << std::setprecision(2) << theta
                  << std::setw(22) << std::scientific << std::setprecision(3) << error
                  << std::setw(12) << std::fixed << std::setprecision(2) << ms
                  << "sum|a| = " << std::setprecision(4) << checksum << "\n";
    }
}

void printEnergyStability() {
    std::cout << "\n=== energy stability (velocity-Verlet, N = 1000, 4000 steps) ===\n";
    bh::SimulationParams params;
    params.theta = 0.4;
    params.softening = 0.5;
    params.timeStep = 2e-3;

    std::vector<bh::Body> bodies =
        bh::makeUniformDisc(1000, 50.0, 1000.0, params.gravitationalConstant, 3u);
    bh::Simulator simulator(bodies, params);

    const double startEnergy = simulator.energy();
    const bh::Vec2 startMomentum = bh::totalMomentum(simulator.bodies());
    simulator.run(4000);
    const double endEnergy = simulator.energy();
    const bh::Vec2 endMomentum = bh::totalMomentum(simulator.bodies());

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "initial total energy: " << startEnergy << "\n";
    std::cout << "final   total energy: " << endEnergy << "\n";
    std::cout << "relative drift:       " << std::scientific << std::setprecision(3)
              << std::fabs((endEnergy - startEnergy) / startEnergy) << "\n";
    std::cout << "initial |momentum|:   " << bh::length(startMomentum) << "\n";
    std::cout << "final   |momentum|:   " << bh::length(endMomentum) << "\n";
}

void printTwoBodyOrbit() {
    std::cout << "\n=== two-body circular orbit (analytic separation = 1) ===\n";
    bh::SimulationParams params;
    params.theta = 0.5;
    params.softening = 1e-6;
    params.timeStep = 1e-4;

    bh::Simulator simulator(bh::makeTwoBodyCircularOrbit(1.0, 1.0, 1.0, 1.0), params);
    const double period = 6.283185307179586 / std::sqrt(2.0);
    const int stepsPerPeriod = static_cast<int>(period / params.timeStep);

    double minSeparation = 1e30;
    double maxSeparation = 0.0;
    for (int orbit = 0; orbit < 5; ++orbit) {
        for (int i = 0; i < stepsPerPeriod; ++i) {
            simulator.step();
            const double separation =
                bh::length(simulator.bodies()[1].position - simulator.bodies()[0].position);
            minSeparation = std::min(minSeparation, separation);
            maxSeparation = std::max(maxSeparation, separation);
        }
    }
    std::cout << std::fixed << std::setprecision(9);
    std::cout << "orbits simulated: 5, steps per orbit: " << stepsPerPeriod << "\n";
    std::cout << "separation min:   " << minSeparation << "\n";
    std::cout << "separation max:   " << maxSeparation << "\n";
    std::cout << "final separation: "
              << bh::length(simulator.bodies()[1].position - simulator.bodies()[0].position)
              << "\n";
}

}

int main() {
    printScalingComparison();
    printAccuracySweep();
    printEnergyStability();
    printTwoBodyOrbit();
    return 0;
}
