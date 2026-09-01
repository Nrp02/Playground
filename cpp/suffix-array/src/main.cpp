#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "suffix_array.hpp"

namespace {

std::string generateText(std::size_t size, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 4);
    std::string text;
    text.reserve(size);
    const std::string alphabet = "abcde";
    while (text.size() < size) {
        text.push_back(alphabet[static_cast<std::size_t>(dist(rng))]);
    }
    return text;
}

std::size_t naiveCount(const std::string& text, const std::string& pattern) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    return count;
}

}

int main() {
    std::string text = generateText(300000, 42);
    const std::string repeatedChunk = "the_quick_brown_fox_jumps_over_";
    std::string repeatedRegion;
    for (int i = 0; i < 400; ++i) {
        repeatedRegion += repeatedChunk;
    }
    text.insert(text.size() / 2, repeatedRegion);

    std::cout << "Text length: " << text.size() << "\n";

    const auto buildStart = std::chrono::steady_clock::now();
    suffix_array::SuffixArray sa(text);
    const auto buildEnd = std::chrono::steady_clock::now();
    const auto buildMs = std::chrono::duration_cast<std::chrono::milliseconds>(buildEnd - buildStart).count();
    std::cout << "Suffix array + LCP construction took " << buildMs << " ms\n";

    const std::vector<std::string> patterns = {"the_quick_brown_fox", "zzzzzzz", "abcde"};
    for (const auto& pattern : patterns) {
        const auto searchStart = std::chrono::steady_clock::now();
        const std::size_t occurrences = sa.count(pattern);
        const auto searchEnd = std::chrono::steady_clock::now();
        const auto searchUs = std::chrono::duration_cast<std::chrono::microseconds>(searchEnd - searchStart).count();

        const auto naiveStart = std::chrono::steady_clock::now();
        const std::size_t naiveOccurrences = naiveCount(text, pattern);
        const auto naiveEnd = std::chrono::steady_clock::now();
        const auto naiveUs = std::chrono::duration_cast<std::chrono::microseconds>(naiveEnd - naiveStart).count();

        std::cout << "Pattern \"" << pattern << "\": " << occurrences
                  << " occurrences (suffix array " << searchUs << " us, naive " << naiveUs << " us)"
                  << (occurrences == naiveOccurrences ? " [match]" : " [MISMATCH]") << "\n";
    }

    const std::string longestRepeated = sa.longestRepeatedSubstring();
    std::cout << "Longest repeated substring length: " << longestRepeated.size() << "\n";
    std::cout << "Longest repeated substring preview: "
              << longestRepeated.substr(0, 60) << (longestRepeated.size() > 60 ? "..." : "") << "\n";

    const std::string textA = generateText(20000, 7) + repeatedChunk + generateText(20000, 8);
    const std::string textB = generateText(15000, 9) + repeatedChunk + generateText(15000, 10);
    const std::string longestCommon = suffix_array::longestCommonSubstring(textA, textB);
    std::cout << "Longest common substring length between two generated texts: " << longestCommon.size() << "\n";
    std::cout << "Longest common substring preview: " << longestCommon.substr(0, 60) << "\n";

    std::cout << "Distinct substring count: " << sa.distinctSubstringCount() << "\n";

    return 0;
}
