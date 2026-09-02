#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "codec.hpp"
#include "image.hpp"

namespace {

std::string formatPsnr(double psnr) {
    if (!(psnr < 1e9)) return "lossless";
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << psnr << " dB";
    return out.str();
}

void printSeparator() {
    std::cout << std::string(78, '-') << "\n";
}

void reportQualitySweep(const jic::Image& original, std::size_t rawBytes, jic::Subsampling mode) {
    std::cout << "quality sweep at " << jic::subsamplingName(mode) << " chroma subsampling\n";
    printSeparator();
    std::cout << std::left << std::setw(10) << "quality" << std::setw(14) << "bytes"
              << std::setw(14) << "ratio" << std::setw(14) << "bits/pixel" << std::setw(12) << "rmse"
              << "psnr\n";
    printSeparator();

    for (int quality : {95, 90, 75, 50, 20, 5}) {
        const std::vector<std::uint8_t> encoded = jic::encode(original, quality, mode);
        const jic::Image decoded = jic::decode(encoded);
        const double ratio = static_cast<double>(rawBytes) / static_cast<double>(encoded.size());
        const double bitsPerPixel = 8.0 * static_cast<double>(encoded.size()) /
                                    (static_cast<double>(original.width) * original.height);
        std::ostringstream ratioText;
        ratioText << std::fixed << std::setprecision(2) << ratio << "x";
        std::ostringstream bppText;
        bppText << std::fixed << std::setprecision(3) << bitsPerPixel;
        std::ostringstream rmseText;
        rmseText << std::fixed << std::setprecision(3) << jic::rootMeanSquaredError(original, decoded);

        std::cout << std::left << std::setw(10) << quality << std::setw(14) << encoded.size()
                  << std::setw(14) << ratioText.str() << std::setw(14) << bppText.str()
                  << std::setw(12) << rmseText.str()
                  << formatPsnr(jic::peakSignalToNoiseRatio(original, decoded)) << "\n";
    }
    printSeparator();
    std::cout << "\n";
}

}

int main(int argc, char** argv) {
    try {
        jic::Image original;
        if (argc > 1) {
            original = jic::readPpmFile(argv[1]);
            std::cout << "loaded " << argv[1] << "\n";
        } else {
            original = jic::makeTestImage(250, 170);
            std::cout << "generated synthetic test image (gradient band, colour blocks, noise band)\n";
        }

        const std::string rawPpm = jic::encodePpm(original);
        const std::size_t rawBytes = rawPpm.size();
        std::cout << "dimensions      : " << original.width << " x " << original.height
                  << (original.width % 8 || original.height % 8 ? "  (not a multiple of 8, padding path exercised)"
                                                                : "")
                  << "\n";
        std::cout << "raw P6 ppm bytes: " << rawBytes << "\n\n";

        reportQualitySweep(original, rawBytes, jic::Subsampling::Quarter420);

        std::cout << "same image, same quality, different chroma subsampling (quality 75)\n";
        printSeparator();
        for (jic::Subsampling mode :
             {jic::Subsampling::Full444, jic::Subsampling::Half422, jic::Subsampling::Quarter420}) {
            const std::vector<std::uint8_t> encoded = jic::encode(original, 75, mode);
            const jic::Image decoded = jic::decode(encoded);
            std::cout << std::left << std::setw(10) << jic::subsamplingName(mode) << std::setw(14)
                      << encoded.size() << std::fixed << std::setprecision(2) << std::setw(10)
                      << static_cast<double>(rawBytes) / static_cast<double>(encoded.size())
                      << "x   psnr " << formatPsnr(jic::peakSignalToNoiseRatio(original, decoded))
                      << "\n";
        }
        printSeparator();
        std::cout << "\n";

        const std::vector<std::uint8_t> rawPixels(original.pixels.begin(), original.pixels.end());
        const std::vector<std::uint8_t> losslessOnly = jic::huffmanCompress(rawPixels);
        const std::vector<std::uint8_t> restored = jic::huffmanDecompress(losslessOnly);
        const std::vector<std::uint8_t> lossy = jic::encode(original, 75, jic::Subsampling::Quarter420);

        std::cout << "where the compression actually comes from\n";
        printSeparator();
        std::cout << "huffman only, no dct/quantisation : " << losslessOnly.size() << " bytes ("
                  << std::fixed << std::setprecision(2)
                  << static_cast<double>(rawBytes) / static_cast<double>(losslessOnly.size())
                  << "x, exact round-trip " << (restored == rawPixels ? "verified" : "FAILED") << ")\n";
        std::cout << "full pipeline at quality 75        : " << lossy.size() << " bytes ("
                  << static_cast<double>(rawBytes) / static_cast<double>(lossy.size()) << "x)\n";
        std::cout << "the dct + quantisation + subsampling stages account for a further "
                  << static_cast<double>(losslessOnly.size()) / static_cast<double>(lossy.size())
                  << "x beyond entropy coding alone\n";
        printSeparator();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
