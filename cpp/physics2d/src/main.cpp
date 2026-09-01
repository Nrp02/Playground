#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

#include "world.hpp"

namespace {

std::vector<Vec2> boxPoints(double halfWidth, double halfHeight) {
    return {Vec2(-halfWidth, -halfHeight), Vec2(halfWidth, -halfHeight), Vec2(halfWidth, halfHeight),
            Vec2(-halfWidth, halfHeight)};
}

void demoStack() {
    std::cout << "=== Demo 1: box stack settling ===\n";
    World world(Vec2(0.0, -9.8));
    world.addPolygon(Vec2(0.0, -0.5), boxPoints(25.0, 0.5), 1.0, true);

    std::vector<Body*> boxes;
    int rows = 4;
    double size = 1.0;
    for (int row = 0; row < rows; ++row) {
        int count = rows - row;
        double y = 0.5 + row * (size + 0.02);
        double startX = -0.5 * (count - 1) * (size + 0.02);
        for (int i = 0; i < count; ++i) {
            double x = startX + i * (size + 0.02);
            boxes.push_back(world.addPolygon(Vec2(x, y), boxPoints(size * 0.5, size * 0.5), 1.0, false, 0.1, 0.4));
        }
    }

    double dt = 1.0 / 60.0;
    int steps = 600;
    for (int i = 0; i < steps; ++i) {
        world.step(dt);
    }

    for (std::size_t i = 0; i < boxes.size(); ++i) {
        std::cout << "  box " << i << " settled at (" << std::fixed << std::setprecision(4) << boxes[i]->position.x
                   << ", " << boxes[i]->position.y << ")\n";
    }
    std::cout << "  total kinetic energy after settling: " << world.totalKineticEnergy() << "\n\n";
}

void demoBounce() {
    std::cout << "=== Demo 2: bouncing ball restitution loss ===\n";
    World world(Vec2(0.0, -9.8));
    world.addPolygon(Vec2(0.0, -0.5), boxPoints(10.0, 0.5), 1.0, true);
    Body* ball = world.addCircle(Vec2(0.0, 5.0), 0.5, 1.0, false, 0.7, 0.2);

    double dt = 1.0 / 240.0;
    int steps = 240 * 8;
    double lastVy = ball->linearVelocity.y;
    double maxHeightSinceBounce = ball->position.y;
    int bounceCount = 0;
    for (int i = 0; i < steps && bounceCount < 5; ++i) {
        world.step(dt);
        maxHeightSinceBounce = std::max(maxHeightSinceBounce, ball->position.y);
        if (lastVy < 0.0 && ball->linearVelocity.y > 0.0) {
            std::cout << "  bounce " << bounceCount << ": peak height before bounce = " << std::fixed
                       << std::setprecision(4) << maxHeightSinceBounce << "\n";
            bounceCount++;
            maxHeightSinceBounce = ball->position.y;
        }
        lastVy = ball->linearVelocity.y;
    }
    std::cout << "\n";
}

void demoBroadphaseScale() {
    std::cout << "=== Demo 3: broad-phase scaling ===\n";
    World world(Vec2(0.0, -9.8), 2.0);
    world.addPolygon(Vec2(0.0, -0.5), boxPoints(60.0, 0.5), 1.0, true);

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> xDist(-55.0, 55.0);
    std::uniform_real_distribution<double> yDist(1.0, 80.0);

    int n = 300;
    for (int i = 0; i < n; ++i) {
        world.addCircle(Vec2(xDist(rng), yDist(rng)), 0.3, 1.0, false, 0.1, 0.3);
    }

    auto start = std::chrono::steady_clock::now();
    double dt = 1.0 / 60.0;
    for (int i = 0; i < 30; ++i) {
        world.step(dt);
    }
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    std::vector<Body*> pointers = world.bodyPointers();
    std::size_t bruteCount = UniformGrid::bruteForcePairs(pointers).size();
    UniformGrid freshGrid(2.0);
    std::size_t gridCount = freshGrid.computeCandidatePairs(pointers).size();
    std::size_t nAll = pointers.size();
    std::size_t naivePairCount = nAll * (nAll - 1) / 2;

    std::cout << "  bodies: " << nAll << "\n";
    std::cout << "  30 steps took " << ms << " ms\n";
    std::cout << "  O(n^2) pair count: " << naivePairCount << "\n";
    std::cout << "  brute-force AABB overlap pairs: " << bruteCount << "\n";
    std::cout << "  grid candidate pairs: " << gridCount << "\n\n";
}

void demoTunneling() {
    std::cout << "=== Demo 4: fast body vs thin wall (no tunnelling) ===\n";
    World world(Vec2(0.0, 0.0));
    Body* wall = world.addPolygon(Vec2(10.0, 0.0), boxPoints(0.05, 5.0), 1.0, true);
    Body* fastBall = world.addCircle(Vec2(0.0, 0.0), 0.2, 1.0, false, 0.5, 0.1);
    fastBall->linearVelocity = Vec2(400.0, 0.0);

    double dt = 1.0 / 60.0;
    for (int i = 0; i < 5; ++i) {
        world.step(dt);
    }

    double wallLeftFace = wall->position.x - 0.05;
    std::cout << "  wall left face x = " << wallLeftFace << "\n";
    std::cout << "  ball final x = " << fastBall->position.x << " (radius 0.2)\n";
    std::cout << "  tunnelled through wall: " << (fastBall->position.x > wall->position.x + 0.05 ? "YES" : "NO")
               << "\n\n";
}

}

int main() {
    demoStack();
    demoBounce();
    demoBroadphaseScale();
    demoTunneling();
    return 0;
}
