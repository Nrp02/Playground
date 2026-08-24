#ifndef RAYTRACER_RENDERER_HPP
#define RAYTRACER_RENDERER_HPP

#include "camera.hpp"
#include "image.hpp"
#include "scene.hpp"

namespace rt {

struct RenderSettings {
    int width = 800;
    int height = 450;
    int samplesPerPixel = 32;  // anti-aliasing: rays averaged per pixel
    int maxDepth = 6;          // max recursive reflection bounces
    unsigned numThreads = 0;   // 0 = use std::thread::hardware_concurrency()
};

// Renders `scene` as seen by `camera` into a freshly-allocated Image.
//
// Work is split across `settings.numThreads` worker threads using a single
// shared std::atomic<int> row cursor: each thread repeatedly claims the
// next unclaimed row and renders every pixel in it. This is a simple form
// of dynamic load balancing — cheap rows (mostly background/sky) and
// expensive rows (passing through reflective spheres, costing extra
// recursive rays and shadow tests) even out automatically, which a naive
// static "first half of rows to thread A, second half to thread B" split
// would not do. Because every thread only ever writes rows it personally
// claimed, no two threads ever write the same pixel, so the framebuffer
// itself needs no locking.
Image render(const Scene& scene, const Camera& camera, const RenderSettings& settings);

}  // namespace rt

#endif  // RAYTRACER_RENDERER_HPP
