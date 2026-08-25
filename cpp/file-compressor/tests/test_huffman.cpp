#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../src/huffman.hpp"

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

std::vector<std::uint8_t> toBytes(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

}

int main() {
    {
        std::vector<std::uint8_t> input = toBytes(
            "the quick brown fox jumps over the lazy dog the quick brown fox");
        std::vector<std::uint8_t> encoded = huff::compress(input);
        std::vector<std::uint8_t> decoded = huff::decompress(encoded);
        expectEq(decoded, input, "normal text round-trips exactly");
    }

    {
        std::vector<std::uint8_t> input;
        std::vector<std::uint8_t> encoded = huff::compress(input);
        std::vector<std::uint8_t> decoded = huff::decompress(encoded);
        expectTrue(decoded.empty(), "empty input round-trips to empty output");
    }

    {
        std::vector<std::uint8_t> input(1000, static_cast<std::uint8_t>('a'));
        std::vector<std::uint8_t> encoded = huff::compress(input);
        std::vector<std::uint8_t> decoded = huff::decompress(encoded);
        expectEq(decoded, input, "single repeated byte round-trips exactly");
        expectTrue(encoded.size() < input.size(), "single repeated byte compresses smaller than original");
    }

    {
        std::vector<std::uint8_t> input;
        for (int i = 0; i < 256; ++i) {
            input.push_back(static_cast<std::uint8_t>(i));
        }
        std::vector<std::uint8_t> encoded = huff::compress(input);
        std::vector<std::uint8_t> decoded = huff::decompress(encoded);
        expectEq(decoded, input, "all-256-distinct-byte-values input round-trips exactly");
    }

    {
        std::string skewed(2000, 'x');
        for (int i = 0; i < 20; ++i) {
            skewed.push_back(static_cast<char>('a' + (i % 5)));
        }
        std::vector<std::uint8_t> input = toBytes(skewed);
        std::vector<std::uint8_t> encoded = huff::compress(input);
        std::vector<std::uint8_t> decoded = huff::decompress(encoded);
        expectEq(decoded, input, "skewed-frequency input round-trips exactly");
        expectTrue(encoded.size() < input.size(), "skewed-frequency input compresses smaller than original");
    }

    {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 255);
        std::vector<std::uint8_t> input;
        for (int i = 0; i < 5000; ++i) {
            input.push_back(static_cast<std::uint8_t>(dist(rng)));
        }
        std::vector<std::uint8_t> encoded = huff::compress(input);
        std::vector<std::uint8_t> decoded = huff::decompress(encoded);
        expectEq(decoded, input, "random uniform bytes round-trip exactly");
    }

    {
        std::vector<std::uint8_t> input = toBytes("ab");
        std::vector<std::uint8_t> encoded = huff::compress(input);
        std::vector<std::uint8_t> decoded = huff::decompress(encoded);
        expectEq(decoded, input, "two-distinct-symbol input round-trips exactly");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
