#pragma once

#include <array>
#include <cmath>

namespace jic {

constexpr int kBlockDim = 8;
constexpr int kBlockArea = kBlockDim * kBlockDim;
constexpr double kPi = 3.14159265358979323846;

using Block = std::array<double, kBlockArea>;

inline const std::array<double, kBlockArea>& dctBasis() {
    static const std::array<double, kBlockArea> basis = [] {
        std::array<double, kBlockArea> table{};
        for (int u = 0; u < kBlockDim; ++u) {
            const double scale = (u == 0) ? std::sqrt(1.0 / kBlockDim) : std::sqrt(2.0 / kBlockDim);
            for (int x = 0; x < kBlockDim; ++x) {
                table[u * kBlockDim + x] =
                    scale * std::cos((2.0 * x + 1.0) * u * kPi / (2.0 * kBlockDim));
            }
        }
        return table;
    }();
    return basis;
}

inline Block forwardDct(const Block& input) {
    const std::array<double, kBlockArea>& basis = dctBasis();
    Block rows{};
    for (int y = 0; y < kBlockDim; ++y) {
        for (int u = 0; u < kBlockDim; ++u) {
            double sum = 0.0;
            for (int x = 0; x < kBlockDim; ++x) {
                sum += input[y * kBlockDim + x] * basis[u * kBlockDim + x];
            }
            rows[y * kBlockDim + u] = sum;
        }
    }
    Block output{};
    for (int u = 0; u < kBlockDim; ++u) {
        for (int v = 0; v < kBlockDim; ++v) {
            double sum = 0.0;
            for (int y = 0; y < kBlockDim; ++y) {
                sum += rows[y * kBlockDim + u] * basis[v * kBlockDim + y];
            }
            output[v * kBlockDim + u] = sum;
        }
    }
    return output;
}

inline Block inverseDct(const Block& coefficients) {
    const std::array<double, kBlockArea>& basis = dctBasis();
    Block rows{};
    for (int v = 0; v < kBlockDim; ++v) {
        for (int x = 0; x < kBlockDim; ++x) {
            double sum = 0.0;
            for (int u = 0; u < kBlockDim; ++u) {
                sum += coefficients[v * kBlockDim + u] * basis[u * kBlockDim + x];
            }
            rows[v * kBlockDim + x] = sum;
        }
    }
    Block output{};
    for (int x = 0; x < kBlockDim; ++x) {
        for (int y = 0; y < kBlockDim; ++y) {
            double sum = 0.0;
            for (int v = 0; v < kBlockDim; ++v) {
                sum += rows[v * kBlockDim + x] * basis[v * kBlockDim + y];
            }
            output[y * kBlockDim + x] = sum;
        }
    }
    return output;
}

inline const std::array<int, kBlockArea>& zigzagOrder() {
    static const std::array<int, kBlockArea> order = {
        0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
        12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
    return order;
}

}
