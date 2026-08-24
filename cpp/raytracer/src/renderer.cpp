#include "renderer.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <random>
#include <thread>
#include <vector>

namespace rt {

namespace {

// Renders every pixel in `row`, averaging `samplesPerPixel` jittered
// samples per pixel for anti-aliasing. `rng` is owned by the calling
// thread (each worker gets its own generator) since std::mt19937 is not
// thread-safe to share across threads without external synchronization,
// and a shared generator would also serialize all the threads on it.
void renderRow(const Scene& scene, const Camera& camera, const RenderSettings& settings, int row,
                std::mt19937& rng, Image& image) {
    std::uniform_real_distribution<double> jitter(0.0, 1.0);

    for (int x = 0; x < settings.width; ++x) {
        Color accum(0.0, 0.0, 0.0);
        for (int s = 0; s < settings.samplesPerPixel; ++s) {
            double u = (x + jitter(rng)) / (settings.width - 1);
            // Image row 0 is the top of the picture, but the camera's
            // viewport parameter t=0 is its bottom, so flip here.
            double vCoord = (row + jitter(rng)) / (settings.height - 1);
            double t = 1.0 - vCoord;

            Ray r = camera.getRay(u, t);
            accum += scene.rayColor(r, settings.maxDepth);
        }
        accum /= static_cast<double>(settings.samplesPerPixel);
        image.set(x, row, accum);
    }
}

}  // namespace

Image render(const Scene& scene, const Camera& camera, const RenderSettings& settings) {
    Image image(settings.width, settings.height);

    unsigned numThreads = settings.numThreads;
    if (numThreads == 0) {
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) {
            numThreads = 4;  // hardware_concurrency() is allowed to return 0 if unknown
        }
    }
    numThreads = std::max(1u, std::min(numThreads, static_cast<unsigned>(std::max(1, settings.height))));

    std::atomic<int> nextRow{0};

    auto worker = [&]() {
        // Seed each thread's RNG independently (device entropy mixed with
        // the thread id) so different threads don't produce identical
        // jitter patterns, which would show up as correlated AA artifacts.
        std::random_device rd;
        std::seed_seq seed{rd(), rd(), static_cast<unsigned>(
                                            std::hash<std::thread::id>{}(std::this_thread::get_id()))};
        std::mt19937 rng(seed);

        int row;
        while ((row = nextRow.fetch_add(1, std::memory_order_relaxed)) < settings.height) {
            renderRow(scene, camera, settings, row, rng, image);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(numThreads);
    for (unsigned i = 0; i < numThreads; ++i) {
        workers.emplace_back(worker);
    }
    for (auto& t : workers) {
        t.join();
    }

    return image;
}

}  // namespace rt
