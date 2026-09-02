#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace jic {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    Image() = default;

    Image(int w, int h) : width(w), height(h), pixels(static_cast<std::size_t>(w) * h * 3, 0) {
        if (w <= 0 || h <= 0) throw std::runtime_error("image dimensions must be positive");
    }

    std::uint8_t& at(int x, int y, int channel) {
        return pixels[(static_cast<std::size_t>(y) * width + x) * 3 + channel];
    }

    std::uint8_t at(int x, int y, int channel) const {
        return pixels[(static_cast<std::size_t>(y) * width + x) * 3 + channel];
    }

    bool sameSize(const Image& other) const {
        return width == other.width && height == other.height;
    }
};

inline std::uint8_t clampToByte(double value) {
    if (value <= 0.0) return 0;
    if (value >= 255.0) return 255;
    return static_cast<std::uint8_t>(value + 0.5);
}

inline std::string encodePpm(const Image& image, bool binary = true) {
    std::ostringstream out;
    out << (binary ? "P6\n" : "P3\n") << image.width << " " << image.height << "\n255\n";
    if (binary) {
        out.write(reinterpret_cast<const char*>(image.pixels.data()),
                  static_cast<std::streamsize>(image.pixels.size()));
    } else {
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                out << static_cast<int>(image.at(x, y, 0)) << " "
                    << static_cast<int>(image.at(x, y, 1)) << " "
                    << static_cast<int>(image.at(x, y, 2)) << (x + 1 == image.width ? "" : " ");
            }
            out << "\n";
        }
    }
    return out.str();
}

inline Image decodePpm(const std::string& text) {
    std::size_t position = 0;
    auto skipSpaceAndComments = [&]() {
        for (;;) {
            while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) {
                ++position;
            }
            if (position < text.size() && text[position] == '#') {
                while (position < text.size() && text[position] != '\n') ++position;
                continue;
            }
            return;
        }
    };
    auto readToken = [&]() {
        skipSpaceAndComments();
        const std::size_t start = position;
        while (position < text.size() && !std::isspace(static_cast<unsigned char>(text[position]))) {
            ++position;
        }
        if (start == position) throw std::runtime_error("ppm: unexpected end of data");
        return text.substr(start, position - start);
    };
    auto readInt = [&]() {
        const std::string token = readToken();
        for (char c : token) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                throw std::runtime_error("ppm: expected a number");
            }
        }
        return std::stoll(token);
    };

    const std::string magic = readToken();
    if (magic != "P3" && magic != "P6") throw std::runtime_error("ppm: unsupported magic");
    const long long width = readInt();
    const long long height = readInt();
    const long long maxValue = readInt();
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535) {
        throw std::runtime_error("ppm: invalid dimensions");
    }
    if (maxValue != 255) throw std::runtime_error("ppm: only maxval 255 is supported");

    Image image(static_cast<int>(width), static_cast<int>(height));
    if (magic == "P6") {
        if (position < text.size()) ++position;
        if (position + image.pixels.size() > text.size()) {
            throw std::runtime_error("ppm: truncated pixel data");
        }
        std::copy(text.begin() + static_cast<std::ptrdiff_t>(position),
                  text.begin() + static_cast<std::ptrdiff_t>(position + image.pixels.size()),
                  image.pixels.begin());
    } else {
        for (std::size_t i = 0; i < image.pixels.size(); ++i) {
            const long long value = readInt();
            if (value < 0 || value > 255) throw std::runtime_error("ppm: sample out of range");
            image.pixels[i] = static_cast<std::uint8_t>(value);
        }
    }
    return image;
}

inline void writePpmFile(const std::string& path, const Image& image, bool binary = true) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("ppm: cannot open file for writing");
    const std::string data = encodePpm(image, binary);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!file) throw std::runtime_error("ppm: write failed");
}

inline Image readPpmFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("ppm: cannot open file for reading");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return decodePpm(buffer.str());
}

inline Image makeTestImage(int width, int height) {
    Image image(width, height);
    std::mt19937 rng(20260902u);
    std::uniform_int_distribution<int> noise(-50, 50);
    std::uniform_int_distribution<int> tint(-15, 15);

    const int bandOne = height / 3;
    const int bandTwo = (2 * height) / 3;
    const std::uint8_t palette[8][3] = {{220, 30, 40},  {30, 200, 90},  {40, 70, 220}, {240, 210, 40},
                                        {160, 40, 200}, {30, 200, 210}, {245, 245, 245}, {25, 25, 30}};

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (y < bandOne) {
                const double fx = static_cast<double>(x) / std::max(1, width - 1);
                const double fy = static_cast<double>(y) / std::max(1, bandOne);
                image.at(x, y, 0) = clampToByte(255.0 * fx);
                image.at(x, y, 1) = clampToByte(255.0 * (1.0 - fx) * 0.6 + 90.0 * fy);
                image.at(x, y, 2) = clampToByte(60.0 + 160.0 * fy);
            } else if (y < bandTwo) {
                const int index = ((x / 16) + 3 * ((y - bandOne) / 16)) % 8;
                image.at(x, y, 0) = palette[index][0];
                image.at(x, y, 1) = palette[index][1];
                image.at(x, y, 2) = palette[index][2];
            } else {
                const int checker = (((x / 3) + (y / 3)) % 2) ? 200 : 60;
                const int grain = noise(rng);
                image.at(x, y, 0) = clampToByte(checker + grain + tint(rng));
                image.at(x, y, 1) = clampToByte(checker + grain + tint(rng));
                image.at(x, y, 2) = clampToByte(checker + grain + tint(rng));
            }
        }
    }
    return image;
}

inline Image makeGradientImage(int width, int height) {
    Image image(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.at(x, y, 0) = clampToByte(255.0 * x / std::max(1, width - 1));
            image.at(x, y, 1) = clampToByte(255.0 * y / std::max(1, height - 1));
            image.at(x, y, 2) = clampToByte(128.0 + 60.0 * std::sin(0.15 * (x + y)));
        }
    }
    return image;
}

inline double meanSquaredError(const Image& a, const Image& b) {
    if (!a.sameSize(b)) throw std::runtime_error("images differ in size");
    double total = 0.0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        const double diff = static_cast<double>(a.pixels[i]) - static_cast<double>(b.pixels[i]);
        total += diff * diff;
    }
    return total / static_cast<double>(a.pixels.size());
}

inline double rootMeanSquaredError(const Image& a, const Image& b) {
    return std::sqrt(meanSquaredError(a, b));
}

inline double peakSignalToNoiseRatio(const Image& a, const Image& b) {
    const double mse = meanSquaredError(a, b);
    if (mse <= 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

}
