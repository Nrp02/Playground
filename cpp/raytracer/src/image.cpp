#include "image.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace rt {

namespace {

unsigned char toByte(double linear) {
    double gammaCorrected = std::sqrt(std::clamp(linear, 0.0, 1.0));
    int scaled = static_cast<int>(std::round(255.999 * gammaCorrected));
    return static_cast<unsigned char>(std::clamp(scaled, 0, 255));
}

}  // namespace

bool Image::writePPM(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "Failed to open '" << path << "' for writing\n";
        return false;
    }

    out << "P6\n" << width_ << ' ' << height_ << "\n255\n";

    std::vector<unsigned char> row(static_cast<size_t>(width_) * 3);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Color& c = at(x, y);
            row[x * 3 + 0] = toByte(c.x);
            row[x * 3 + 1] = toByte(c.y);
            row[x * 3 + 2] = toByte(c.z);
        }
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }

    if (!out) {
        std::cerr << "Error while writing '" << path << "'\n";
        return false;
    }
    return true;
}

}  // namespace rt
