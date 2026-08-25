#include <iostream>
#include <string>
#include <vector>

#include "huffman.hpp"

namespace {

std::vector<std::uint8_t> toBytes(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

}

int main() {
    std::string sentence = "The quick brown fox jumps over the lazy dog. ";
    std::string paragraph;
    for (int i = 0; i < 200; ++i) {
        paragraph += sentence;
    }

    std::vector<std::uint8_t> original = toBytes(paragraph);
    std::vector<std::uint8_t> encoded = huff::compress(original);
    std::vector<std::uint8_t> decoded = huff::decompress(encoded);

    std::cout << "=== huffman file compressor demo ===\n";
    std::cout << "original size: " << original.size() << " bytes\n";
    std::cout << "encoded size:  " << encoded.size() << " bytes\n";
    std::cout << "round-trip matches original: " << std::boolalpha << (decoded == original) << "\n";
    std::cout << "compression ratio: " << huff::compressionRatio(original.size(), encoded.size()) << "x\n";

    std::cout << "\n=== edge case: empty input ===\n";
    std::vector<std::uint8_t> empty;
    std::vector<std::uint8_t> emptyEncoded = huff::compress(empty);
    std::vector<std::uint8_t> emptyDecoded = huff::decompress(emptyEncoded);
    std::cout << "empty round-trip matches: " << (emptyDecoded == empty) << "\n";

    std::cout << "\n=== edge case: single repeated byte ===\n";
    std::vector<std::uint8_t> single(500, static_cast<std::uint8_t>('z'));
    std::vector<std::uint8_t> singleEncoded = huff::compress(single);
    std::vector<std::uint8_t> singleDecoded = huff::decompress(singleEncoded);
    std::cout << "single-symbol original size: " << single.size() << " bytes\n";
    std::cout << "single-symbol encoded size:  " << singleEncoded.size() << " bytes\n";
    std::cout << "single-symbol round-trip matches: " << (singleDecoded == single) << "\n";

    return 0;
}
