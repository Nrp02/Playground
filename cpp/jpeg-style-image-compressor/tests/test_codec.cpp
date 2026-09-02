#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../src/bit_io.hpp"
#include "../src/codec.hpp"
#include "../src/dct.hpp"
#include "../src/huffman.hpp"
#include "../src/image.hpp"

namespace {

int g_failures = 0;

void expectTrue(bool condition, const std::string& testName) {
    if (!condition) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

template <typename T>
void expectEq(const T& actual, const T& expected, const std::string& testName) {
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

template <typename Callable>
bool throwsRuntimeError(Callable callable) {
    try {
        callable();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

std::vector<jic::Token> tokensForBlocks(const std::vector<std::array<int, jic::kBlockArea>>& blocks,
                                        bool chroma) {
    std::vector<jic::Token> tokens;
    int predictor = 0;
    for (const std::array<int, jic::kBlockArea>& block : blocks) {
        predictor = jic::tokenizeBlock(tokens, block, predictor, chroma);
    }
    return tokens;
}

}

int main() {
    {
        jic::Block constant{};
        constant.fill(42.0);
        const jic::Block coefficients = jic::forwardDct(constant);
        bool acZero = true;
        for (int i = 1; i < jic::kBlockArea; ++i) {
            if (std::fabs(coefficients[static_cast<std::size_t>(i)]) > 1e-9) acZero = false;
        }
        expectTrue(acZero, "dct of a constant block has zero ac coefficients");
        expectTrue(std::fabs(coefficients[0] - 42.0 * 8.0) < 1e-9,
                   "dct of a constant block puts all energy in dc");
    }

    {
        std::mt19937 rng(1234u);
        std::uniform_real_distribution<double> values(-128.0, 127.0);
        double worst = 0.0;
        for (int trial = 0; trial < 200; ++trial) {
            jic::Block input{};
            for (double& sample : input) sample = values(rng);
            const jic::Block restored = jic::inverseDct(jic::forwardDct(input));
            for (int i = 0; i < jic::kBlockArea; ++i) {
                worst = std::max(worst, std::fabs(input[static_cast<std::size_t>(i)] -
                                                  restored[static_cast<std::size_t>(i)]));
            }
        }
        expectTrue(worst < 1e-9, "forward dct followed by inverse dct round-trips a block");
    }

    {
        const std::array<int, jic::kBlockArea>& order = jic::zigzagOrder();
        std::array<int, jic::kBlockArea> seen{};
        bool inRange = true;
        for (int position : order) {
            if (position < 0 || position >= jic::kBlockArea) {
                inRange = false;
                break;
            }
            ++seen[static_cast<std::size_t>(position)];
        }
        bool bijection = inRange;
        for (int count : seen) {
            if (count != 1) bijection = false;
        }
        expectTrue(bijection, "zigzag order is a bijection over the 64 block positions");
        expectEq(order[0], 0, "zigzag starts at the dc coefficient");
        expectEq(order[jic::kBlockArea - 1], 63, "zigzag ends at the highest frequency coefficient");
    }

    {
        double worst = 0.0;
        bool byteExact = true;
        for (int r = 0; r <= 255; r += 5) {
            for (int g = 0; g <= 255; g += 5) {
                for (int b = 0; b <= 255; b += 15) {
                    double y = 0.0;
                    double cb = 0.0;
                    double cr = 0.0;
                    jic::rgbToYcbcr(r, g, b, y, cb, cr);
                    double br = 0.0;
                    double bg = 0.0;
                    double bb = 0.0;
                    jic::ycbcrToRgb(y, cb, cr, br, bg, bb);
                    worst = std::max({worst, std::fabs(br - r), std::fabs(bg - g), std::fabs(bb - b)});
                    if (jic::clampToByte(br) != r || jic::clampToByte(bg) != g ||
                        jic::clampToByte(bb) != b) {
                        byteExact = false;
                    }
                }
            }
        }
        expectTrue(byteExact, "rgb to ycbcr to rgb round-trips every colour byte exactly");
        expectTrue(worst < 1e-3,
                   "the standard jpeg conversion constants invert to better than a thousandth");
    }

    {
        bool magnitudeOk = true;
        for (int value = -4000; value <= 4000; ++value) {
            const int category = jic::magnitudeCategory(value);
            const std::uint32_t bits = jic::magnitudeBits(value, category);
            if (jic::magnitudeValue(bits, category) != value) magnitudeOk = false;
        }
        expectTrue(magnitudeOk, "magnitude category coding round-trips every coefficient value");
        expectEq(jic::magnitudeCategory(0), 0, "zero has magnitude category zero");
        expectEq(jic::magnitudeCategory(1), 1, "one has magnitude category one");
        expectEq(jic::magnitudeCategory(-7), 3, "minus seven has magnitude category three");
    }

    {
        std::mt19937 rng(99u);
        std::uniform_int_distribution<int> lengths(1, 17);
        std::vector<std::pair<std::uint32_t, int>> written;
        jic::BitWriter writer;
        for (int i = 0; i < 500; ++i) {
            const int count = lengths(rng);
            std::uniform_int_distribution<std::uint32_t> values(
                0, count >= 32 ? 0xFFFFFFFFu : ((1u << count) - 1u));
            const std::uint32_t value = values(rng);
            writer.writeBits(value, count);
            written.emplace_back(value, count);
        }
        const std::vector<std::uint8_t> bytes = writer.finish();
        expectEq(bytes.size(), (writer.bitCount() + 7) / 8, "bit writer pads to a whole byte count");

        jic::BitReader reader(bytes.data(), bytes.size(), writer.bitCount());
        bool matched = true;
        for (const std::pair<std::uint32_t, int>& entry : written) {
            std::uint32_t value = 0;
            if (!reader.readBits(entry.second, value) || value != entry.first) matched = false;
        }
        expectTrue(matched, "bit reader recovers every written field at unaligned lengths");
        expectTrue(reader.exhausted(), "bit reader lands exactly on the written bit count");
        std::uint32_t overflow = 0;
        expectTrue(!reader.readBits(1, overflow), "bit reader refuses to read past the bit limit");
    }

    {
        jic::BitWriter writer;
        writer.writeBits(1, 1);
        writer.writeBits(0, 1);
        writer.writeBits(5, 3);
        const std::vector<std::uint8_t> bytes = writer.finish();
        expectEq(bytes.size(), static_cast<std::size_t>(1), "five bits occupy a single byte");
        expectEq(writer.bitCount(), static_cast<std::size_t>(5), "bit writer counts written bits");
        expectEq(static_cast<int>(bytes[0]), 0b10101000, "unaligned bits are packed msb first");
    }

    {
        jic::FrequencyTable frequencies{};
        frequencies['a'] = 45;
        frequencies['b'] = 13;
        frequencies['c'] = 12;
        frequencies['d'] = 16;
        frequencies['e'] = 9;
        frequencies['f'] = 5;
        const jic::HuffmanTable table = jic::buildHuffmanTable(frequencies);
        expectTrue(table.lengths['a'] < table.lengths['f'],
                   "the most frequent symbol gets a shorter code than the rarest");
        expectEq(table.sortedSymbols.size(), static_cast<std::size_t>(6),
                 "canonical table holds every used symbol");

        std::vector<std::uint8_t> serialized;
        jic::appendTable(serialized, table);
        std::size_t offset = 0;
        const jic::HuffmanTable restored = jic::readTable(serialized, offset);
        expectEq(offset, serialized.size(), "table deserialisation consumes exactly its bytes");
        expectTrue(restored.lengths == table.lengths && restored.codes == table.codes,
                   "serialised huffman table rebuilds identical canonical codes");

        jic::BitWriter writer;
        const std::string message = "abcdefabcabbaaffedcba";
        for (char c : message) {
            jic::encodeSymbol(writer, table, static_cast<std::uint8_t>(c));
        }
        const std::vector<std::uint8_t> payload = writer.finish();
        jic::BitReader reader(payload.data(), payload.size(), writer.bitCount());
        std::string decoded;
        for (std::size_t i = 0; i < message.size(); ++i) {
            decoded.push_back(static_cast<char>(jic::decodeSymbol(reader, restored)));
        }
        expectEq(decoded, message, "huffman encode and decode round-trip a symbol stream");
    }

    {
        jic::FrequencyTable frequencies{};
        frequencies[7] = 1;
        const jic::HuffmanTable table = jic::buildHuffmanTable(frequencies);
        expectEq(static_cast<int>(table.lengths[7]), 1, "a single-symbol alphabet gets a one bit code");
        jic::BitWriter writer;
        jic::encodeSymbol(writer, table, 7);
        jic::encodeSymbol(writer, table, 7);
        const std::vector<std::uint8_t> payload = writer.finish();
        jic::BitReader reader(payload.data(), payload.size(), writer.bitCount());
        expectTrue(jic::decodeSymbol(reader, table) == 7 && jic::decodeSymbol(reader, table) == 7,
                   "a single-symbol table still round-trips");
    }

    {
        jic::FrequencyTable frequencies{};
        std::uint64_t weight = 1;
        for (int i = 0; i < 40; ++i) {
            frequencies[static_cast<std::size_t>(i)] = weight;
            if (weight < (1ull << 60)) weight *= 2;
        }
        const jic::HuffmanTable table = jic::buildHuffmanTable(frequencies);
        int longest = 0;
        for (std::uint8_t symbol : table.sortedSymbols) {
            longest = std::max(longest, static_cast<int>(table.lengths[symbol]));
        }
        expectTrue(longest <= jic::kMaxCodeLength,
                   "skewed frequencies are length limited to sixteen bit codes");
    }

    {
        std::vector<std::array<int, jic::kBlockArea>> blocks;
        std::array<int, jic::kBlockArea> flat{};
        flat[0] = -300;
        blocks.push_back(flat);

        std::array<int, jic::kBlockArea> sparse{};
        sparse[0] = 17;
        sparse[1] = -4;
        sparse[40] = 9;
        sparse[63] = -1;
        blocks.push_back(sparse);

        std::array<int, jic::kBlockArea> longRun{};
        longRun[0] = 17;
        longRun[35] = 2;
        blocks.push_back(longRun);

        std::array<int, jic::kBlockArea> dense{};
        for (int i = 0; i < jic::kBlockArea; ++i) {
            dense[static_cast<std::size_t>(i)] = ((i % 5) - 2) * (i + 1);
        }
        blocks.push_back(dense);

        const std::vector<jic::Token> tokens = tokensForBlocks(blocks, false);
        bool sawZeroRunLength = false;
        bool sawEndOfBlock = false;
        for (const jic::Token& token : tokens) {
            if (token.table == jic::kAcLuma && token.symbol == jic::kZeroRunLength) {
                sawZeroRunLength = true;
            }
            if (token.table == jic::kAcLuma && token.symbol == jic::kEndOfBlock) sawEndOfBlock = true;
        }
        expectTrue(sawZeroRunLength, "runs longer than fifteen zeros emit a zero-run-length symbol");
        expectTrue(sawEndOfBlock, "a block ending in zeros emits an end-of-block symbol");

        std::array<jic::FrequencyTable, 4> frequencies{};
        for (const jic::Token& token : tokens) {
            ++frequencies[token.table][token.symbol];
        }
        std::array<jic::HuffmanTable, 4> tables;
        for (int i = 0; i < 4; ++i) {
            tables[static_cast<std::size_t>(i)] =
                jic::buildHuffmanTable(frequencies[static_cast<std::size_t>(i)]);
        }
        jic::BitWriter writer;
        for (const jic::Token& token : tokens) {
            jic::encodeSymbol(writer, tables[token.table], token.symbol);
            if (token.extraBits > 0) writer.writeBits(token.extra, token.extraBits);
        }
        const std::vector<std::uint8_t> payload = writer.finish();

        jic::BitReader reader(payload.data(), payload.size(), writer.bitCount());
        std::vector<std::array<int, jic::kBlockArea>> restored;
        int predictor = 0;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            int newDc = 0;
            restored.push_back(jic::decodeBlockCoefficients(reader, tables[jic::kDcLuma],
                                                            tables[jic::kAcLuma], predictor, newDc));
            predictor = newDc;
        }
        expectTrue(restored == blocks,
                   "run-length plus huffman coding round-trips hand-built coefficient blocks");
        expectTrue(reader.exhausted(), "block decoding consumes the whole payload");
    }

    {
        std::vector<std::array<int, jic::kBlockArea>> blocks;
        for (int i = 0; i < 6; ++i) {
            std::array<int, jic::kBlockArea> block{};
            block[0] = 100 - 37 * i;
            blocks.push_back(block);
        }
        const std::vector<jic::Token> tokens = tokensForBlocks(blocks, true);
        int dcTokens = 0;
        for (const jic::Token& token : tokens) {
            if (token.table == jic::kDcChroma) ++dcTokens;
        }
        expectEq(dcTokens, 6, "each block contributes exactly one differentially coded dc symbol");
        expectTrue(tokens[0].symbol == static_cast<std::uint8_t>(jic::magnitudeCategory(100)),
                   "the first dc difference is coded against a zero predictor");
    }

    {
        const std::array<int, jic::kBlockArea> high = jic::scaledQuantizationTable(false, 100);
        const std::array<int, jic::kBlockArea> mid = jic::scaledQuantizationTable(false, 50);
        const std::array<int, jic::kBlockArea> low = jic::scaledQuantizationTable(false, 10);
        expectEq(mid, jic::baseLuminanceTable(), "quality fifty reproduces the base jpeg table");
        bool ordered = true;
        bool clamped = true;
        for (int i = 0; i < jic::kBlockArea; ++i) {
            const std::size_t index = static_cast<std::size_t>(i);
            if (!(high[index] <= mid[index] && mid[index] <= low[index])) ordered = false;
            if (high[index] < 1 || low[index] > 255) clamped = false;
        }
        expectTrue(ordered, "quantisation steps grow monotonically as quality falls");
        expectTrue(clamped, "quantisation entries stay inside the one to 255 range");
        expectTrue(throwsRuntimeError([] { jic::scaledQuantizationTable(false, 0); }),
                   "quality zero is rejected");
        expectTrue(throwsRuntimeError([] { jic::scaledQuantizationTable(false, 101); }),
                   "quality above one hundred is rejected");
    }

    {
        jic::Plane source(5, 3);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 5; ++x) {
                source.at(x, y) = 10.0 * y + x;
            }
        }
        const jic::Plane small = jic::downsamplePlane(source, 2, 2);
        expectEq(small.width, 3, "odd widths round up when downsampling");
        expectEq(small.height, 2, "odd heights round up when downsampling");
        expectTrue(std::fabs(small.at(0, 0) - (0.0 + 1.0 + 10.0 + 11.0) / 4.0) < 1e-9,
                   "downsampling averages the covered samples");
        expectTrue(std::fabs(small.at(2, 1) - 24.0) < 1e-9,
                   "downsampling a partial edge cell averages only real samples");
        const jic::Plane back = jic::upsamplePlane(small, 2, 2, 5, 3);
        expectEq(back.width, 5, "upsampling restores the original width");
        expectEq(back.height, 3, "upsampling restores the original height");
        expectTrue(std::fabs(back.at(1, 0) - small.at(0, 0)) < 1e-9,
                   "upsampling replicates each chroma sample across its cell");
    }

    {
        const jic::Image image = jic::makeTestImage(37, 21);
        const std::string binary = jic::encodePpm(image, true);
        const std::string text = jic::encodePpm(image, false);
        expectTrue(jic::decodePpm(binary).pixels == image.pixels, "p6 ppm round-trips the pixels");
        expectTrue(jic::decodePpm(text).pixels == image.pixels, "p3 ppm round-trips the pixels");
        expectTrue(throwsRuntimeError([] { jic::decodePpm("P5\n2 2\n255\n"); }),
                   "an unsupported ppm magic is rejected");
        expectTrue(throwsRuntimeError([] { jic::decodePpm("P6\n2 2\n255\nabc"); }),
                   "a truncated ppm body is rejected");
        expectTrue(throwsRuntimeError([] { jic::decodePpm("P3\n0 4\n255\n"); }),
                   "a zero sized ppm is rejected");

        const std::string path = "bin/roundtrip_scratch.ppm";
        jic::writePpmFile(path, image);
        const jic::Image loaded = jic::readPpmFile(path);
        std::remove(path.c_str());
        expectTrue(loaded.pixels == image.pixels && loaded.width == image.width,
                   "ppm file write and read round-trip the image");
    }

    {
        const jic::Image image = jic::makeTestImage(53, 29);
        const jic::Image smooth = jic::makeGradientImage(53, 29);
        expectTrue(image.width % 8 != 0 && image.height % 8 != 0,
                   "the padding test image is not a multiple of the block size");
        for (jic::Subsampling mode : {jic::Subsampling::Full444, jic::Subsampling::Half422,
                                      jic::Subsampling::Quarter420}) {
            const std::string label = jic::subsamplingName(mode);
            const jic::Image decoded = jic::decode(jic::encode(image, 90, mode));
            expectTrue(decoded.width == image.width && decoded.height == image.height,
                       "decoded dimensions survive the padding path for " + label);
            const jic::Image decodedSmooth = jic::decode(jic::encode(smooth, 90, mode));
            expectTrue(decodedSmooth.width == smooth.width && decodedSmooth.height == smooth.height,
                       "decoded dimensions survive the padding path on a gradient for " + label);
            expectTrue(jic::peakSignalToNoiseRatio(smooth, decodedSmooth) > 30.0,
                       "padded round-trip of a smooth image stays faithful for " + label);
        }
        expectTrue(jic::peakSignalToNoiseRatio(image, jic::decode(jic::encode(image, 90,
                                                                             jic::Subsampling::Full444))) >
                       jic::peakSignalToNoiseRatio(image, jic::decode(jic::encode(
                                                              image, 90, jic::Subsampling::Quarter420))),
                   "chroma subsampling costs accuracy on a high frequency image");
    }

    {
        const jic::Image flat(19, 11);
        const std::vector<std::uint8_t> encoded = jic::encode(flat, 95, jic::Subsampling::Quarter420);
        const jic::Image decoded = jic::decode(encoded);
        expectTrue(jic::meanSquaredError(flat, decoded) < 1e-9,
                   "a uniform image survives the full pipeline exactly");
    }

    {
        const jic::Image image = jic::makeGradientImage(64, 48);
        const jic::Image best = jic::decode(jic::encode(image, 98, jic::Subsampling::Full444));
        const jic::Image worst = jic::decode(jic::encode(image, 10, jic::Subsampling::Quarter420));
        const double bestError = jic::rootMeanSquaredError(image, best);
        const double worstError = jic::rootMeanSquaredError(image, worst);
        expectTrue(bestError < worstError, "higher quality yields a lower reconstruction error");
        expectTrue(worstError > 3.0 * bestError,
                   "quality ten is substantially worse than quality ninety eight");

        double previousError = -1.0;
        bool monotonic = true;
        std::size_t previousSize = 0;
        bool shrinking = true;
        for (int quality : {95, 80, 60, 40, 20, 5}) {
            const std::vector<std::uint8_t> encoded = jic::encode(image, quality);
            const double error = jic::rootMeanSquaredError(image, jic::decode(encoded));
            if (previousError >= 0.0 && error < previousError - 1e-9) monotonic = false;
            if (previousSize > 0 && encoded.size() > previousSize) shrinking = false;
            previousError = error;
            previousSize = encoded.size();
        }
        expectTrue(monotonic, "error grows as quality falls across the whole sweep");
        expectTrue(shrinking, "compressed size shrinks as quality falls across the whole sweep");
    }

    {
        const jic::Image image = jic::makeTestImage(40, 40);
        const std::vector<std::uint8_t> full = jic::encode(image, 75, jic::Subsampling::Full444);
        const std::vector<std::uint8_t> half = jic::encode(image, 75, jic::Subsampling::Quarter420);
        expectTrue(half.size() < full.size(), "4:2:0 subsampling produces a smaller stream than 4:4:4");
        const std::size_t rawBytes = static_cast<std::size_t>(image.width) * image.height * 3;
        expectTrue(half.size() < rawBytes, "the compressed stream is smaller than the raw pixels");
    }

    {
        const jic::Image image = jic::makeTestImage(24, 24);
        const std::vector<std::uint8_t> encoded = jic::encode(image, 75, jic::Subsampling::Quarter420);

        expectTrue(throwsRuntimeError([] { jic::decode(std::vector<std::uint8_t>()); }),
                   "an empty stream is rejected");
        expectTrue(throwsRuntimeError([&] {
                       std::vector<std::uint8_t> corrupt = encoded;
                       corrupt[0] = 'X';
                       jic::decode(corrupt);
                   }),
                   "a bad magic is rejected");
        expectTrue(throwsRuntimeError([&] {
                       std::vector<std::uint8_t> corrupt = encoded;
                       corrupt[4] = 9;
                       jic::decode(corrupt);
                   }),
                   "an unknown version is rejected");
        expectTrue(throwsRuntimeError([&] {
                       std::vector<std::uint8_t> corrupt = encoded;
                       corrupt[9] = 0;
                       jic::decode(corrupt);
                   }),
                   "an out of range quality in the header is rejected");
        expectTrue(throwsRuntimeError([&] {
                       std::vector<std::uint8_t> corrupt = encoded;
                       corrupt[10] = 7;
                       jic::decode(corrupt);
                   }),
                   "an unknown subsampling mode is rejected");

        bool everyTruncationRejected = true;
        for (std::size_t keep = 1; keep < encoded.size(); keep += 3) {
            const std::vector<std::uint8_t> truncated(encoded.begin(),
                                                      encoded.begin() +
                                                          static_cast<std::ptrdiff_t>(keep));
            if (!throwsRuntimeError([&] { jic::decode(truncated); })) {
                everyTruncationRejected = false;
                break;
            }
        }
        expectTrue(everyTruncationRejected,
                   "every truncation of a valid stream is rejected instead of crashing");

        std::mt19937 rng(7u);
        std::uniform_int_distribution<int> byteValues(0, 255);
        int survived = 0;
        for (int trial = 0; trial < 200; ++trial) {
            std::vector<std::uint8_t> corrupt = encoded;
            std::uniform_int_distribution<std::size_t> positions(5, corrupt.size() - 1);
            corrupt[positions(rng)] = static_cast<std::uint8_t>(byteValues(rng));
            try {
                const jic::Image decoded = jic::decode(corrupt);
                if (decoded.width == image.width && decoded.height == image.height) ++survived;
            } catch (const std::exception&) {
                ++survived;
            }
        }
        expectEq(survived, 200, "randomly corrupted streams either decode or throw, never hang");
    }

    {
        const jic::Image image = jic::makeTestImage(48, 32);
        const std::vector<std::uint8_t> pixels(image.pixels.begin(), image.pixels.end());
        const std::vector<std::uint8_t> compressed = jic::huffmanCompress(pixels);
        expectTrue(jic::huffmanDecompress(compressed) == pixels,
                   "the lossless huffman path round-trips the raw pixels exactly");
        expectTrue(compressed.size() < pixels.size(),
                   "entropy coding alone still shrinks the raw pixels");
        const std::vector<std::uint8_t> lossy = jic::encode(image, 75, jic::Subsampling::Quarter420);
        expectTrue(lossy.size() < compressed.size(),
                   "the lossy transform stage beats entropy coding alone by a wide margin");
        expectTrue(throwsRuntimeError([&] {
                       std::vector<std::uint8_t> corrupt = compressed;
                       corrupt.resize(corrupt.size() / 2);
                       jic::huffmanDecompress(corrupt);
                   }),
                   "a truncated lossless stream is rejected");

        const std::vector<std::uint8_t> empty;
        expectTrue(jic::huffmanDecompress(jic::huffmanCompress(empty)) == empty,
                   "the lossless path handles an empty input");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
