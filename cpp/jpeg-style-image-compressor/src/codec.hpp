#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "bit_io.hpp"
#include "dct.hpp"
#include "huffman.hpp"
#include "image.hpp"

namespace jic {

enum class Subsampling : std::uint8_t { Full444 = 0, Half422 = 1, Quarter420 = 2 };

inline int chromaHorizontalFactor(Subsampling mode) {
    return mode == Subsampling::Full444 ? 1 : 2;
}

inline int chromaVerticalFactor(Subsampling mode) {
    return mode == Subsampling::Quarter420 ? 2 : 1;
}

inline std::string subsamplingName(Subsampling mode) {
    switch (mode) {
        case Subsampling::Full444: return "4:4:4";
        case Subsampling::Half422: return "4:2:2";
        case Subsampling::Quarter420: return "4:2:0";
    }
    return "unknown";
}

inline const std::array<int, kBlockArea>& baseLuminanceTable() {
    static const std::array<int, kBlockArea> table = {
        16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
        14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
        18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
        49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
    return table;
}

inline const std::array<int, kBlockArea>& baseChrominanceTable() {
    static const std::array<int, kBlockArea> table = {
        17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
    return table;
}

inline std::array<int, kBlockArea> scaledQuantizationTable(bool chroma, int quality) {
    if (quality < 1 || quality > 100) throw std::runtime_error("quality must be in 1..100");
    const int scale = (quality < 50) ? (5000 / quality) : (200 - 2 * quality);
    const std::array<int, kBlockArea>& base = chroma ? baseChrominanceTable() : baseLuminanceTable();
    std::array<int, kBlockArea> result{};
    for (int i = 0; i < kBlockArea; ++i) {
        int value = (base[static_cast<std::size_t>(i)] * scale + 50) / 100;
        value = std::max(1, std::min(255, value));
        result[static_cast<std::size_t>(i)] = value;
    }
    return result;
}

inline void rgbToYcbcr(double r, double g, double b, double& y, double& cb, double& cr) {
    y = 0.299 * r + 0.587 * g + 0.114 * b;
    cb = 128.0 - 0.168736 * r - 0.331264 * g + 0.5 * b;
    cr = 128.0 + 0.5 * r - 0.418688 * g - 0.081312 * b;
}

inline void ycbcrToRgb(double y, double cb, double cr, double& r, double& g, double& b) {
    r = y + 1.402 * (cr - 128.0);
    g = y - 0.344136 * (cb - 128.0) - 0.714136 * (cr - 128.0);
    b = y + 1.772 * (cb - 128.0);
}

struct Plane {
    int width = 0;
    int height = 0;
    std::vector<double> samples;

    Plane() = default;
    Plane(int w, int h) : width(w), height(h), samples(static_cast<std::size_t>(w) * h, 0.0) {}

    double& at(int x, int y) { return samples[static_cast<std::size_t>(y) * width + x]; }
    double at(int x, int y) const { return samples[static_cast<std::size_t>(y) * width + x]; }

    double clamped(int x, int y) const {
        const int cx = std::min(std::max(x, 0), width - 1);
        const int cy = std::min(std::max(y, 0), height - 1);
        return at(cx, cy);
    }
};

inline void imageToPlanes(const Image& image, Plane& y, Plane& cb, Plane& cr) {
    y = Plane(image.width, image.height);
    cb = Plane(image.width, image.height);
    cr = Plane(image.width, image.height);
    for (int row = 0; row < image.height; ++row) {
        for (int col = 0; col < image.width; ++col) {
            double ly = 0.0;
            double lcb = 0.0;
            double lcr = 0.0;
            rgbToYcbcr(image.at(col, row, 0), image.at(col, row, 1), image.at(col, row, 2), ly, lcb, lcr);
            y.at(col, row) = ly;
            cb.at(col, row) = lcb;
            cr.at(col, row) = lcr;
        }
    }
}

inline Image planesToImage(const Plane& y, const Plane& cb, const Plane& cr) {
    Image image(y.width, y.height);
    for (int row = 0; row < image.height; ++row) {
        for (int col = 0; col < image.width; ++col) {
            double r = 0.0;
            double g = 0.0;
            double b = 0.0;
            ycbcrToRgb(y.at(col, row), cb.at(col, row), cr.at(col, row), r, g, b);
            image.at(col, row, 0) = clampToByte(r);
            image.at(col, row, 1) = clampToByte(g);
            image.at(col, row, 2) = clampToByte(b);
        }
    }
    return image;
}

inline Plane downsamplePlane(const Plane& source, int horizontal, int vertical) {
    const int width = (source.width + horizontal - 1) / horizontal;
    const int height = (source.height + vertical - 1) / vertical;
    Plane result(width, height);
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            double total = 0.0;
            int count = 0;
            for (int dy = 0; dy < vertical; ++dy) {
                for (int dx = 0; dx < horizontal; ++dx) {
                    const int sx = col * horizontal + dx;
                    const int sy = row * vertical + dy;
                    if (sx >= source.width || sy >= source.height) continue;
                    total += source.at(sx, sy);
                    ++count;
                }
            }
            result.at(col, row) = count > 0 ? total / count : 128.0;
        }
    }
    return result;
}

inline Plane upsamplePlane(const Plane& source, int horizontal, int vertical, int targetWidth,
                           int targetHeight) {
    Plane result(targetWidth, targetHeight);
    for (int row = 0; row < targetHeight; ++row) {
        for (int col = 0; col < targetWidth; ++col) {
            result.at(col, row) = source.clamped(col / horizontal, row / vertical);
        }
    }
    return result;
}

inline int magnitudeCategory(int value) {
    int category = 0;
    int magnitude = value < 0 ? -value : value;
    while (magnitude > 0) {
        ++category;
        magnitude >>= 1;
    }
    return category;
}

inline std::uint32_t magnitudeBits(int value, int category) {
    if (category == 0) return 0;
    const std::uint32_t mask = (category >= 32) ? 0xFFFFFFFFu : ((1u << category) - 1u);
    if (value > 0) return static_cast<std::uint32_t>(value) & mask;
    return static_cast<std::uint32_t>(value + (1 << category) - 1) & mask;
}

inline int magnitudeValue(std::uint32_t bits, int category) {
    if (category == 0) return 0;
    const std::uint32_t half = 1u << (category - 1);
    if (bits < half) return static_cast<int>(bits) - (1 << category) + 1;
    return static_cast<int>(bits);
}

struct Token {
    std::uint8_t table = 0;
    std::uint8_t symbol = 0;
    std::uint32_t extra = 0;
    std::uint8_t extraBits = 0;
};

constexpr std::uint8_t kDcLuma = 0;
constexpr std::uint8_t kAcLuma = 1;
constexpr std::uint8_t kDcChroma = 2;
constexpr std::uint8_t kAcChroma = 3;
constexpr std::uint8_t kEndOfBlock = 0x00;
constexpr std::uint8_t kZeroRunLength = 0xF0;

inline int tokenizeBlock(std::vector<Token>& out, const std::array<int, kBlockArea>& zigzag,
                         int previousDc, bool chroma) {
    const std::uint8_t dcTable = chroma ? kDcChroma : kDcLuma;
    const std::uint8_t acTable = chroma ? kAcChroma : kAcLuma;

    const int difference = zigzag[0] - previousDc;
    const int dcCategory = magnitudeCategory(difference);
    if (dcCategory > 15) throw std::runtime_error("dc difference out of range");
    out.push_back(Token{dcTable, static_cast<std::uint8_t>(dcCategory),
                        magnitudeBits(difference, dcCategory), static_cast<std::uint8_t>(dcCategory)});

    int run = 0;
    for (int i = 1; i < kBlockArea; ++i) {
        const int value = zigzag[static_cast<std::size_t>(i)];
        if (value == 0) {
            ++run;
            continue;
        }
        while (run >= 16) {
            out.push_back(Token{acTable, kZeroRunLength, 0, 0});
            run -= 16;
        }
        const int category = magnitudeCategory(value);
        if (category > 15) throw std::runtime_error("ac coefficient out of range");
        const std::uint8_t symbol = static_cast<std::uint8_t>((run << 4) | category);
        out.push_back(Token{acTable, symbol, magnitudeBits(value, category),
                            static_cast<std::uint8_t>(category)});
        run = 0;
    }
    if (run > 0) out.push_back(Token{acTable, kEndOfBlock, 0, 0});
    return zigzag[0];
}

inline std::array<int, kBlockArea> decodeBlockCoefficients(BitReader& reader,
                                                           const HuffmanTable& dcTable,
                                                           const HuffmanTable& acTable,
                                                           int previousDc, int& newDc) {
    std::array<int, kBlockArea> zigzag{};
    const int dcCategory = decodeSymbol(reader, dcTable);
    if (dcCategory > 15) throw std::runtime_error("invalid dc category");
    std::uint32_t bits = 0;
    if (dcCategory > 0 && !reader.readBits(dcCategory, bits)) {
        throw std::runtime_error("bit stream exhausted reading dc value");
    }
    zigzag[0] = previousDc + magnitudeValue(bits, dcCategory);
    newDc = zigzag[0];

    int index = 1;
    while (index < kBlockArea) {
        const std::uint8_t symbol = decodeSymbol(reader, acTable);
        const int run = symbol >> 4;
        const int category = symbol & 0x0F;
        if (category == 0) {
            if (run == 15) {
                index += 16;
                continue;
            }
            break;
        }
        index += run;
        if (index >= kBlockArea) throw std::runtime_error("ac run length overflows block");
        std::uint32_t valueBits = 0;
        if (!reader.readBits(category, valueBits)) {
            throw std::runtime_error("bit stream exhausted reading ac value");
        }
        zigzag[static_cast<std::size_t>(index)] = magnitudeValue(valueBits, category);
        ++index;
    }
    return zigzag;
}

inline std::vector<std::array<int, kBlockArea>> quantizePlane(
    const Plane& plane, const std::array<int, kBlockArea>& quantTable) {
    const int blocksAcross = (plane.width + kBlockDim - 1) / kBlockDim;
    const int blocksDown = (plane.height + kBlockDim - 1) / kBlockDim;
    const std::array<int, kBlockArea>& order = zigzagOrder();

    std::vector<std::array<int, kBlockArea>> blocks;
    blocks.reserve(static_cast<std::size_t>(blocksAcross) * blocksDown);
    for (int by = 0; by < blocksDown; ++by) {
        for (int bx = 0; bx < blocksAcross; ++bx) {
            Block spatial{};
            for (int y = 0; y < kBlockDim; ++y) {
                for (int x = 0; x < kBlockDim; ++x) {
                    spatial[static_cast<std::size_t>(y * kBlockDim + x)] =
                        plane.clamped(bx * kBlockDim + x, by * kBlockDim + y) - 128.0;
                }
            }
            const Block coefficients = forwardDct(spatial);
            std::array<int, kBlockArea> quantized{};
            for (int i = 0; i < kBlockArea; ++i) {
                const int position = order[static_cast<std::size_t>(i)];
                const double scaled = coefficients[static_cast<std::size_t>(position)] /
                                      quantTable[static_cast<std::size_t>(position)];
                quantized[static_cast<std::size_t>(i)] = static_cast<int>(std::llround(scaled));
            }
            blocks.push_back(quantized);
        }
    }
    return blocks;
}

inline Plane reconstructPlane(const std::vector<std::array<int, kBlockArea>>& blocks, int width,
                              int height, const std::array<int, kBlockArea>& quantTable) {
    const int blocksAcross = (width + kBlockDim - 1) / kBlockDim;
    const int blocksDown = (height + kBlockDim - 1) / kBlockDim;
    if (blocks.size() != static_cast<std::size_t>(blocksAcross) * blocksDown) {
        throw std::runtime_error("block count does not match plane dimensions");
    }
    const std::array<int, kBlockArea>& order = zigzagOrder();

    Plane plane(width, height);
    for (int by = 0; by < blocksDown; ++by) {
        for (int bx = 0; bx < blocksAcross; ++bx) {
            const std::array<int, kBlockArea>& quantized =
                blocks[static_cast<std::size_t>(by) * blocksAcross + bx];
            Block coefficients{};
            for (int i = 0; i < kBlockArea; ++i) {
                const int position = order[static_cast<std::size_t>(i)];
                coefficients[static_cast<std::size_t>(position)] =
                    static_cast<double>(quantized[static_cast<std::size_t>(i)]) *
                    quantTable[static_cast<std::size_t>(position)];
            }
            const Block spatial = inverseDct(coefficients);
            for (int y = 0; y < kBlockDim; ++y) {
                const int py = by * kBlockDim + y;
                if (py >= height) break;
                for (int x = 0; x < kBlockDim; ++x) {
                    const int px = bx * kBlockDim + x;
                    if (px >= width) break;
                    plane.at(px, py) = spatial[static_cast<std::size_t>(y * kBlockDim + x)] + 128.0;
                }
            }
        }
    }
    return plane;
}

constexpr std::size_t kHeaderSize = 11;

inline void appendLittleEndian(std::vector<std::uint8_t>& out, std::uint32_t value, int byteCount) {
    for (int i = 0; i < byteCount; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
    }
}

inline std::uint32_t readLittleEndian(const std::vector<std::uint8_t>& data, std::size_t& offset,
                                      int byteCount) {
    if (offset + static_cast<std::size_t>(byteCount) > data.size()) {
        throw std::runtime_error("truncated header field");
    }
    std::uint32_t value = 0;
    for (int i = 0; i < byteCount; ++i) {
        value |= static_cast<std::uint32_t>(data[offset + static_cast<std::size_t>(i)]) << (8 * i);
    }
    offset += static_cast<std::size_t>(byteCount);
    return value;
}

inline std::vector<std::uint8_t> encode(const Image& image, int quality,
                                        Subsampling mode = Subsampling::Quarter420) {
    if (image.width <= 0 || image.height <= 0) throw std::runtime_error("empty image");
    if (image.width > 65535 || image.height > 65535) throw std::runtime_error("image too large");
    const std::array<int, kBlockArea> lumaTable = scaledQuantizationTable(false, quality);
    const std::array<int, kBlockArea> chromaTable = scaledQuantizationTable(true, quality);

    Plane y;
    Plane cb;
    Plane cr;
    imageToPlanes(image, y, cb, cr);
    const int horizontal = chromaHorizontalFactor(mode);
    const int vertical = chromaVerticalFactor(mode);
    const Plane smallCb = downsamplePlane(cb, horizontal, vertical);
    const Plane smallCr = downsamplePlane(cr, horizontal, vertical);

    const std::vector<std::array<int, kBlockArea>> yBlocks = quantizePlane(y, lumaTable);
    const std::vector<std::array<int, kBlockArea>> cbBlocks = quantizePlane(smallCb, chromaTable);
    const std::vector<std::array<int, kBlockArea>> crBlocks = quantizePlane(smallCr, chromaTable);

    std::vector<Token> tokens;
    tokens.reserve((yBlocks.size() + cbBlocks.size() + crBlocks.size()) * 8);
    int predictor = 0;
    for (const std::array<int, kBlockArea>& block : yBlocks) {
        predictor = tokenizeBlock(tokens, block, predictor, false);
    }
    predictor = 0;
    for (const std::array<int, kBlockArea>& block : cbBlocks) {
        predictor = tokenizeBlock(tokens, block, predictor, true);
    }
    predictor = 0;
    for (const std::array<int, kBlockArea>& block : crBlocks) {
        predictor = tokenizeBlock(tokens, block, predictor, true);
    }

    std::array<FrequencyTable, 4> frequencies{};
    for (const Token& token : tokens) {
        ++frequencies[token.table][token.symbol];
    }
    std::array<HuffmanTable, 4> tables;
    for (int i = 0; i < 4; ++i) {
        tables[static_cast<std::size_t>(i)] =
            buildHuffmanTable(frequencies[static_cast<std::size_t>(i)]);
    }

    BitWriter writer;
    for (const Token& token : tokens) {
        encodeSymbol(writer, tables[token.table], token.symbol);
        if (token.extraBits > 0) writer.writeBits(token.extra, token.extraBits);
    }
    const std::vector<std::uint8_t> payload = writer.finish();

    std::vector<std::uint8_t> out;
    out.push_back('J');
    out.push_back('I');
    out.push_back('C');
    out.push_back('1');
    out.push_back(1);
    appendLittleEndian(out, static_cast<std::uint32_t>(image.width), 2);
    appendLittleEndian(out, static_cast<std::uint32_t>(image.height), 2);
    out.push_back(static_cast<std::uint8_t>(quality));
    out.push_back(static_cast<std::uint8_t>(mode));
    for (const HuffmanTable& table : tables) {
        appendTable(out, table);
    }
    appendLittleEndian(out, static_cast<std::uint32_t>(writer.bitCount()), 4);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

inline Image decode(const std::vector<std::uint8_t>& data) {
    if (data.size() < kHeaderSize) throw std::runtime_error("stream too short for header");
    if (data[0] != 'J' || data[1] != 'I' || data[2] != 'C' || data[3] != '1') {
        throw std::runtime_error("bad magic");
    }
    if (data[4] != 1) throw std::runtime_error("unsupported format version");

    std::size_t offset = 5;
    const int width = static_cast<int>(readLittleEndian(data, offset, 2));
    const int height = static_cast<int>(readLittleEndian(data, offset, 2));
    const int quality = data[offset++];
    const std::uint8_t modeByte = data[offset++];
    if (width <= 0 || height <= 0) throw std::runtime_error("invalid dimensions in header");
    if (quality < 1 || quality > 100) throw std::runtime_error("invalid quality in header");
    if (modeByte > 2) throw std::runtime_error("invalid subsampling mode in header");
    const Subsampling mode = static_cast<Subsampling>(modeByte);

    std::array<HuffmanTable, 4> tables;
    for (int i = 0; i < 4; ++i) {
        tables[static_cast<std::size_t>(i)] = readTable(data, offset);
    }
    const std::uint32_t bitCount = readLittleEndian(data, offset, 4);
    const std::size_t availableBits = (data.size() - offset) * 8;
    if (static_cast<std::size_t>(bitCount) > availableBits) {
        throw std::runtime_error("declared payload is larger than the stream");
    }

    const int horizontal = chromaHorizontalFactor(mode);
    const int vertical = chromaVerticalFactor(mode);
    const int chromaWidth = (width + horizontal - 1) / horizontal;
    const int chromaHeight = (height + vertical - 1) / vertical;
    const std::size_t yBlockCount = static_cast<std::size_t>((width + kBlockDim - 1) / kBlockDim) *
                                    static_cast<std::size_t>((height + kBlockDim - 1) / kBlockDim);
    const std::size_t chromaBlockCount =
        static_cast<std::size_t>((chromaWidth + kBlockDim - 1) / kBlockDim) *
        static_cast<std::size_t>((chromaHeight + kBlockDim - 1) / kBlockDim);

    BitReader reader(data.data() + offset, data.size() - offset, bitCount);
    auto readBlocks = [&](std::size_t count, bool chroma) {
        const HuffmanTable& dcTable = chroma ? tables[kDcChroma] : tables[kDcLuma];
        const HuffmanTable& acTable = chroma ? tables[kAcChroma] : tables[kAcLuma];
        std::vector<std::array<int, kBlockArea>> blocks;
        blocks.reserve(count);
        int predictor = 0;
        for (std::size_t i = 0; i < count; ++i) {
            int newDc = 0;
            blocks.push_back(decodeBlockCoefficients(reader, dcTable, acTable, predictor, newDc));
            predictor = newDc;
        }
        return blocks;
    };

    const std::vector<std::array<int, kBlockArea>> yBlocks = readBlocks(yBlockCount, false);
    const std::vector<std::array<int, kBlockArea>> cbBlocks = readBlocks(chromaBlockCount, true);
    const std::vector<std::array<int, kBlockArea>> crBlocks = readBlocks(chromaBlockCount, true);

    const std::array<int, kBlockArea> lumaTable = scaledQuantizationTable(false, quality);
    const std::array<int, kBlockArea> chromaTable = scaledQuantizationTable(true, quality);
    const Plane y = reconstructPlane(yBlocks, width, height, lumaTable);
    const Plane smallCb = reconstructPlane(cbBlocks, chromaWidth, chromaHeight, chromaTable);
    const Plane smallCr = reconstructPlane(crBlocks, chromaWidth, chromaHeight, chromaTable);
    const Plane cb = upsamplePlane(smallCb, horizontal, vertical, width, height);
    const Plane cr = upsamplePlane(smallCr, horizontal, vertical, width, height);
    return planesToImage(y, cb, cr);
}

inline std::vector<std::uint8_t> huffmanCompress(const std::vector<std::uint8_t>& data) {
    FrequencyTable frequencies{};
    for (std::uint8_t byte : data) {
        ++frequencies[byte];
    }
    const HuffmanTable table = buildHuffmanTable(frequencies);

    BitWriter writer;
    for (std::uint8_t byte : data) {
        encodeSymbol(writer, table, byte);
    }
    const std::vector<std::uint8_t> payload = writer.finish();

    std::vector<std::uint8_t> out;
    out.push_back('J');
    out.push_back('I');
    out.push_back('C');
    out.push_back('H');
    appendLittleEndian(out, static_cast<std::uint32_t>(data.size()), 4);
    appendTable(out, table);
    appendLittleEndian(out, static_cast<std::uint32_t>(writer.bitCount()), 4);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

inline std::vector<std::uint8_t> huffmanDecompress(const std::vector<std::uint8_t>& data) {
    if (data.size() < 8) throw std::runtime_error("stream too short for header");
    if (data[0] != 'J' || data[1] != 'I' || data[2] != 'C' || data[3] != 'H') {
        throw std::runtime_error("bad magic");
    }
    std::size_t offset = 4;
    const std::uint32_t originalSize = readLittleEndian(data, offset, 4);
    const HuffmanTable table = readTable(data, offset);
    const std::uint32_t bitCount = readLittleEndian(data, offset, 4);
    if (static_cast<std::size_t>(bitCount) > (data.size() - offset) * 8) {
        throw std::runtime_error("declared payload is larger than the stream");
    }
    if (originalSize > 0 && table.empty()) throw std::runtime_error("missing huffman table");

    BitReader reader(data.data() + offset, data.size() - offset, bitCount);
    std::vector<std::uint8_t> out;
    out.reserve(originalSize);
    for (std::uint32_t i = 0; i < originalSize; ++i) {
        out.push_back(decodeSymbol(reader, table));
    }
    return out;
}

}
