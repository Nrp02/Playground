#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../src/aho_corasick.hpp"

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

std::vector<ac::Match> bruteForce(const std::vector<std::string>& patterns, const std::string& text,
                                   bool caseInsensitive) {
    auto normalize = [caseInsensitive](std::string s) {
        if (caseInsensitive) {
            for (char& c : s) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        return s;
    };
    std::string normalizedText = normalize(text);
    std::vector<ac::Match> result;
    for (int patternIndex = 0; patternIndex < static_cast<int>(patterns.size()); ++patternIndex) {
        std::string pattern = normalize(patterns[patternIndex]);
        if (pattern.empty()) {
            continue;
        }
        std::size_t position = normalizedText.find(pattern, 0);
        while (position != std::string::npos) {
            result.push_back(ac::Match{patternIndex, position + pattern.size() - 1});
            position = normalizedText.find(pattern, position + 1);
        }
    }
    std::sort(result.begin(), result.end(), [](const ac::Match& a, const ac::Match& b) {
        if (a.endPosition != b.endPosition) {
            return a.endPosition < b.endPosition;
        }
        return a.patternIndex < b.patternIndex;
    });
    return result;
}

std::vector<ac::Match> sortedMatches(std::vector<ac::Match> matches) {
    std::sort(matches.begin(), matches.end(), [](const ac::Match& a, const ac::Match& b) {
        if (a.endPosition != b.endPosition) {
            return a.endPosition < b.endPosition;
        }
        return a.patternIndex < b.patternIndex;
    });
    return matches;
}

bool matchesEqual(const std::vector<ac::Match>& a, const std::vector<ac::Match>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].patternIndex != b[i].patternIndex || a[i].endPosition != b[i].endPosition) {
            return false;
        }
    }
    return true;
}

std::string randomAlphabetString(std::mt19937& rng, std::size_t length, const std::string& alphabet) {
    std::uniform_int_distribution<std::size_t> dist(0, alphabet.size() - 1);
    std::string result(length, ' ');
    for (std::size_t i = 0; i < length; ++i) {
        result[i] = alphabet[dist(rng)];
    }
    return result;
}

}

int main() {
    {
        ac::AhoCorasick automaton;
        automaton.addPattern("he");
        automaton.addPattern("she");
        automaton.addPattern("his");
        automaton.addPattern("hers");
        automaton.build();
        std::vector<ac::Match> matches = sortedMatches(automaton.scan("ushers"));
        std::vector<ac::Match> expected{{0, 3}, {1, 3}, {3, 5}};
        expectTrue(matchesEqual(matches, expected), "textbook he/she/his/hers overlap example");
    }

    {
        ac::AhoCorasick automaton;
        automaton.addPattern("a");
        automaton.addPattern("aa");
        automaton.addPattern("aaa");
        automaton.build();
        std::vector<ac::Match> matches = sortedMatches(automaton.scan("aaaa"));
        expectEq<std::size_t>(matches.size(), 9, "overlapping nested patterns all reported");
    }

    {
        ac::AhoCorasick automaton;
        automaton.addPattern("ab");
        automaton.addPattern("ab");
        automaton.build();
        std::vector<ac::Match> matches = automaton.scan("ab");
        expectEq<std::size_t>(matches.size(), 2, "duplicate patterns each produce their own match");
    }

    {
        ac::AhoCorasick automaton;
        automaton.addPattern("hello");
        automaton.build();
        std::vector<ac::Match> matches = automaton.scan("hello");
        expectEq<std::size_t>(matches.size(), 1, "pattern equal to whole text matches once");
        if (!matches.empty()) {
            expectEq<std::size_t>(matches[0].endPosition, static_cast<std::size_t>(4), "match ends at last index");
        }
    }

    {
        ac::AhoCorasick automaton;
        automaton.addPattern("x");
        automaton.build();
        std::vector<ac::Match> matches = automaton.scan("");
        expectTrue(matches.empty(), "empty text yields no matches");
    }

    {
        ac::AhoCorasick automaton;
        automaton.addPattern("a");
        automaton.addPattern("b");
        automaton.addPattern("c");
        automaton.build();
        std::vector<ac::Match> matches = automaton.scan("abc");
        expectEq<std::size_t>(matches.size(), 3, "single character patterns all found");
    }

    {
        ac::AhoCorasick automaton(true);
        automaton.addPattern("Secret");
        automaton.build();
        std::vector<ac::Match> matches = automaton.scan("this is SECRET and secret");
        expectEq<std::size_t>(matches.size(), 2, "case insensitive mode matches both cases");
    }

    {
        ac::AhoCorasick automaton;
        automaton.addPattern("secret");
        automaton.addPattern("password");
        automaton.build();
        std::string redacted = automaton.replaceAll("the secret password here", "***");
        expectEq<std::string>(redacted, "the *** *** here", "replaceAll redacts non overlapping matches");
    }

    {
        std::mt19937 rng(7);
        const std::string alphabet = "ab";
        for (int trial = 0; trial < 200; ++trial) {
            std::vector<std::string> patterns;
            int patternCount = 1 + static_cast<int>(trial % 5);
            for (int i = 0; i < patternCount; ++i) {
                std::size_t length = 1 + (rng() % 3);
                patterns.push_back(randomAlphabetString(rng, length, alphabet));
            }
            std::string text = randomAlphabetString(rng, 20, alphabet);

            ac::AhoCorasick automaton;
            for (const std::string& pattern : patterns) {
                automaton.addPattern(pattern);
            }
            automaton.build();

            std::vector<ac::Match> actual = sortedMatches(automaton.scan(text));
            std::vector<ac::Match> expected = bruteForce(patterns, text, false);
            expectTrue(matchesEqual(actual, expected), "randomized small alphabet trial " + std::to_string(trial));
        }
    }

    {
        std::mt19937 rng(99);
        const std::string alphabet = "abc";
        std::vector<std::string> patterns{"ab", "bc", "abc", "a", "cba", "bcab"};
        std::string text = randomAlphabetString(rng, 500, alphabet);

        ac::AhoCorasick automaton;
        for (const std::string& pattern : patterns) {
            automaton.addPattern(pattern);
        }
        automaton.build();

        std::vector<ac::Match> singleShot = sortedMatches(automaton.scan(text));

        for (int trial = 0; trial < 20; ++trial) {
            ac::AhoCorasick::StreamingScanner scanner = automaton.streamingScanner();
            std::vector<ac::Match> streamed;
            std::size_t offset = 0;
            std::uniform_int_distribution<std::size_t> chunkDist(1, 17);
            while (offset < text.size()) {
                std::size_t chunkSize = std::min(chunkDist(rng), text.size() - offset);
                std::vector<ac::Match> chunkMatches = scanner.feed(text.substr(offset, chunkSize));
                streamed.insert(streamed.end(), chunkMatches.begin(), chunkMatches.end());
                offset += chunkSize;
            }
            expectTrue(matchesEqual(sortedMatches(streamed), singleShot),
                       "streaming chunked scan matches single shot trial " + std::to_string(trial));
        }
    }

    std::cout << g_failures << " failing tests\n";
    return g_failures == 0 ? 0 : 1;
}
