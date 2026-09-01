#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "aho_corasick.hpp"

namespace {

std::string randomString(std::mt19937& rng, std::size_t length) {
    static const std::string alphabet = "abcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<std::size_t> dist(0, alphabet.size() - 1);
    std::string result(length, ' ');
    for (std::size_t i = 0; i < length; ++i) {
        result[i] = alphabet[dist(rng)];
    }
    return result;
}

std::size_t naiveCount(const std::vector<std::string>& patterns, const std::string& text) {
    std::size_t total = 0;
    for (const std::string& pattern : patterns) {
        std::size_t position = text.find(pattern, 0);
        while (position != std::string::npos) {
            ++total;
            position = text.find(pattern, position + 1);
        }
    }
    return total;
}

}

int main() {
    std::mt19937 rng(42);
    std::vector<std::string> patterns;
    for (int i = 0; i < 3000; ++i) {
        patterns.push_back(randomString(rng, 4 + (i % 6)));
    }

    ac::AhoCorasick automaton;
    for (const std::string& pattern : patterns) {
        automaton.addPattern(pattern);
    }
    automaton.build();

    std::string text = randomString(rng, 400000);

    auto acStart = std::chrono::steady_clock::now();
    std::vector<ac::Match> matches = automaton.scan(text);
    auto acEnd = std::chrono::steady_clock::now();

    std::cout << "Aho-Corasick pass: " << matches.size() << " matches in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(acEnd - acStart).count()
              << " ms\n";

    auto naiveStart = std::chrono::steady_clock::now();
    std::size_t naiveMatches = naiveCount(patterns, text);
    auto naiveEnd = std::chrono::steady_clock::now();

    std::cout << "Naive per-pattern find: " << naiveMatches << " matches in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(naiveEnd - naiveStart).count()
              << " ms\n";

    ac::AhoCorasick::StreamingScanner scanner = automaton.streamingScanner();
    std::vector<ac::Match> streamedMatches;
    std::size_t offset = 0;
    std::uniform_int_distribution<std::size_t> chunkDist(1, 4000);
    while (offset < text.size()) {
        std::size_t chunkSize = std::min(chunkDist(rng), text.size() - offset);
        std::vector<ac::Match> chunkMatches = scanner.feed(text.substr(offset, chunkSize));
        streamedMatches.insert(streamedMatches.end(), chunkMatches.begin(), chunkMatches.end());
        offset += chunkSize;
    }

    bool identical = streamedMatches.size() == matches.size();
    if (identical) {
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (matches[i].patternIndex != streamedMatches[i].patternIndex ||
                matches[i].endPosition != streamedMatches[i].endPosition) {
                identical = false;
                break;
            }
        }
    }
    std::cout << "Streaming scan matches single-shot scan: " << (identical ? "yes" : "no") << "\n";

    ac::AhoCorasick redactor;
    redactor.addPattern("secret");
    redactor.addPattern("password");
    redactor.addPattern("classified");
    redactor.build();
    std::string sample = "the secret password is classified information";
    std::cout << redactor.replaceAll(sample, "[REDACTED]") << "\n";

    return 0;
}
