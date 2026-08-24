#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "camera.hpp"
#include "renderer.hpp"
#include "scene.hpp"

namespace {

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog
              << " [output.ppm] [width] [height] [samples_per_pixel] [max_depth] [threads]\n"
              << "  All arguments are optional and positional; defaults render a\n"
              << "  demo scene to output.ppm at 800x450 with 32 samples/pixel.\n";
}

}  // namespace

int main(int argc, char** argv) {
    rt::RenderSettings settings;
    std::string outputPath = "output.ppm";

    if (argc > 1 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        printUsage(argv[0]);
        return EXIT_SUCCESS;
    }

    if (argc > 1) outputPath = argv[1];
    if (argc > 2) settings.width = std::max(1, std::atoi(argv[2]));
    if (argc > 3) settings.height = std::max(1, std::atoi(argv[3]));
    if (argc > 4) settings.samplesPerPixel = std::max(1, std::atoi(argv[4]));
    if (argc > 5) settings.maxDepth = std::max(1, std::atoi(argv[5]));
    if (argc > 6) settings.numThreads = static_cast<unsigned>(std::max(0, std::atoi(argv[6])));

    unsigned threadsReported = settings.numThreads != 0 ? settings.numThreads : std::thread::hardware_concurrency();
    std::cout << "Multithreaded ray tracer\n"
              << "  output:      " << outputPath << '\n'
              << "  resolution:  " << settings.width << "x" << settings.height << '\n'
              << "  samples/px:  " << settings.samplesPerPixel << '\n'
              << "  max depth:   " << settings.maxDepth << '\n'
              << "  threads:     " << (threadsReported == 0 ? "auto" : std::to_string(threadsReported)) << '\n';

    rt::Scene scene = rt::buildDemoScene();
    std::cout << "Scene built: " << scene.objectCount() << " objects, " << scene.lightCount() << " light(s)\n";

    double aspectRatio = static_cast<double>(settings.width) / static_cast<double>(settings.height);
    rt::Camera camera(/*lookFrom=*/rt::Point3(0.0, 2.6, 9.0),
                       /*lookAt=*/rt::Point3(0.0, 0.75, 0.0),
                       /*vup=*/rt::Vec3(0.0, 1.0, 0.0),
                       /*verticalFovDegrees=*/35.0, aspectRatio);

    std::cout << "Rendering..." << std::flush;
    auto start = std::chrono::steady_clock::now();

    rt::Image image = rt::render(scene, camera, settings);

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();
    std::cout << " done in " << seconds << "s\n";

    if (!image.writePPM(outputPath)) {
        std::cerr << "Failed to write " << outputPath << '\n';
        return EXIT_FAILURE;
    }

    long long totalRays =
        static_cast<long long>(settings.width) * settings.height * settings.samplesPerPixel;
    std::cout << "Wrote " << outputPath << " (" << settings.width << "x" << settings.height << ", "
              << totalRays << " primary samples, " << (totalRays / seconds / 1e6) << " Msamples/s)\n";

    return EXIT_SUCCESS;
}
