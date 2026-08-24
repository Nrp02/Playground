#ifndef RAYTRACER_IMAGE_HPP
#define RAYTRACER_IMAGE_HPP

#include <string>
#include <vector>

#include "vec3.hpp"

namespace rt {

// Owns the render's framebuffer. Render threads each write to a disjoint
// set of rows (see Renderer), so Image itself does not need any locking:
// concurrent writes to different elements of `pixels_` are safe as long as
// no two threads touch the same index, which the row-partitioning scheme
// guarantees.
class Image {
public:
    Image(int width, int height) : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height) {}

    int width() const { return width_; }
    int height() const { return height_; }

    void set(int x, int y, const Color& c) { pixels_[index(x, y)] = c; }
    const Color& at(int x, int y) const { return pixels_[index(x, y)]; }

    // Writes the framebuffer to `path` as a binary (P6) PPM file. Each
    // linear color component is gamma-corrected (gamma = 2, i.e. sqrt) and
    // clamped to [0,255] before being written as a single byte, which is
    // the standard way small ray tracers avoid needing a real display
    // color-management pipeline.
    bool writePPM(const std::string& path) const;

private:
    size_t index(int x, int y) const { return static_cast<size_t>(y) * width_ + x; }

    int width_;
    int height_;
    std::vector<Color> pixels_;
};

}  // namespace rt

#endif  // RAYTRACER_IMAGE_HPP
