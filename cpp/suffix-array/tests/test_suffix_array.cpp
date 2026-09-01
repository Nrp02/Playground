#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../src/suffix_array.hpp"

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
        std::cerr << "FAIL: " << testName << " (expected " << expected << ", got " << actual << ")\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

std::vector<int> bruteForceSA(const std::string& text) {
    const int n = static_cast<int>(text.size());
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) {
        idx[i] = i;
    }
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return text.substr(a) < text.substr(b);
    });
    return idx;
}

int commonPrefixLen(const std::string& text, int a, int b) {
    int n = static_cast<int>(text.size());
    int len = 0;
    while (a + len < n && b + len < n && text[a + len] == text[b + len]) {
        ++len;
    }
    return len;
}

std::vector<int> bruteForceLCP(const std::string& text, const std::vector<int>& sa) {
    std::vector<int> lcp(sa.size(), 0);
    for (std::size_t i = 1; i < sa.size(); ++i) {
        lcp[i] = commonPrefixLen(text, sa[i - 1], sa[i]);
    }
    return lcp;
}

std::vector<int> naiveFindAll(const std::string& text, const std::string& pattern) {
    std::vector<int> result;
    if (pattern.empty()) {
        for (std::size_t i = 0; i <= text.size(); ++i) {
            result.push_back(static_cast<int>(i));
        }
        return result;
    }
    std::size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        result.push_back(static_cast<int>(pos));
        ++pos;
    }
    return result;
}

int bruteLongestRepeatedLen(const std::string& text) {
    const int n = static_cast<int>(text.size());
    int best = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            best = std::max(best, commonPrefixLen(text, i, j));
        }
    }
    return best;
}

std::size_t bruteDistinctSubstringCount(const std::string& text) {
    std::set<std::string> substrings;
    const std::size_t n = text.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t len = 1; i + len <= n; ++len) {
            substrings.insert(text.substr(i, len));
        }
    }
    return substrings.size();
}

int bruteLongestCommonSubstringLen(const std::string& a, const std::string& b) {
    int best = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t len = 1; i + len <= a.size(); ++len) {
            const std::string candidate = a.substr(i, len);
            if (b.find(candidate) != std::string::npos) {
                best = std::max(best, static_cast<int>(len));
            }
        }
    }
    return best;
}

void checkSaAndLcp(const std::string& text, const std::string& label) {
    const auto sa = suffix_array::buildSuffixArray(text);
    const auto lcp = suffix_array::buildLcpArray(text, sa);
    const auto expectedSa = bruteForceSA(text);
    const auto expectedLcp = bruteForceLCP(text, expectedSa);
    expectTrue(sa == expectedSa, label + ": suffix array matches brute force");
    expectTrue(lcp == expectedLcp, label + ": lcp array matches brute force");
}

}

int main() {
    checkSaAndLcp("", "empty string");
    checkSaAndLcp("a", "single character");
    checkSaAndLcp("aaaaaaaaaa", "all same character");
    checkSaAndLcp("abababababab", "alternating characters");
    checkSaAndLcp("banana", "banana");
    checkSaAndLcp("mississippi", "mississippi");

    {
        std::mt19937 rng(123);
        std::uniform_int_distribution<int> lenDist(0, 40);
        for (int alphabetSize : {2, 3, 5}) {
            std::uniform_int_distribution<int> charDist(0, alphabetSize - 1);
            for (int trial = 0; trial < 30; ++trial) {
                const int len = lenDist(rng);
                std::string text;
                text.reserve(len);
                for (int i = 0; i < len; ++i) {
                    text.push_back(static_cast<char>('a' + charDist(rng)));
                }
                checkSaAndLcp(text, "randomized alphabet=" + std::to_string(alphabetSize) + " trial=" + std::to_string(trial));
            }
        }
    }

    {
        const std::string text = "the_quick_brown_fox_the_quick_brown_dog_the_lazy_fox";
        suffix_array::SuffixArray sa(text);

        for (const std::string& pattern : {std::string("the_quick"), std::string("fox"), std::string("zzz"), std::string("o")}) {
            const auto actual = sa.findAll(pattern);
            const auto expected = naiveFindAll(text, pattern);
            expectTrue(actual == expected, "findAll matches naive search for pattern \"" + pattern + "\"");
            expectEq<std::size_t>(sa.count(pattern), expected.size(), "count matches naive occurrence count for \"" + pattern + "\"");
        }
    }

    {
        const std::vector<std::string> texts = {
            "banana",
            "mississippi",
            "aabaabaa",
            "xyzxyzxyzabc",
            "abcabcabc",
            "z",
            "",
        };
        for (const auto& text : texts) {
            suffix_array::SuffixArray sa(text);
            const int expectedLen = bruteLongestRepeatedLen(text);
            expectEq<std::size_t>(sa.longestRepeatedSubstring().size(), static_cast<std::size_t>(expectedLen),
                                   "longestRepeatedSubstring length matches brute force for \"" + text + "\"");
        }
    }

    {
        const std::vector<std::string> texts = {
            "banana",
            "mississippi",
            "aabaabaa",
            "xyzxyzxyzabc",
            "abcabcabc",
            "a",
        };
        for (const auto& text : texts) {
            suffix_array::SuffixArray sa(text);
            const std::size_t expected = bruteDistinctSubstringCount(text);
            expectEq<std::size_t>(sa.distinctSubstringCount(), expected,
                                   "distinctSubstringCount matches brute force for \"" + text + "\"");
        }
        suffix_array::SuffixArray emptySa("");
        expectEq<std::size_t>(emptySa.distinctSubstringCount(), static_cast<std::size_t>(0),
                               "distinctSubstringCount is zero for empty string");
    }

    {
        const std::vector<std::pair<std::string, std::string>> pairs = {
            {"abcdef", "zzzcdefzz"},
            {"banananana", "ananas"},
            {"hello", "world"},
            {"abcabc", "cabcab"},
            {"", "abc"},
            {"xxxxx", "xx"},
        };
        for (const auto& [a, b] : pairs) {
            const std::string common = suffix_array::longestCommonSubstring(a, b);
            const int expectedLen = bruteLongestCommonSubstringLen(a, b);
            expectEq<std::size_t>(common.size(), static_cast<std::size_t>(expectedLen),
                                   "longestCommonSubstring length matches brute force for \"" + a + "\" / \"" + b + "\"");
            if (!common.empty()) {
                expectTrue(a.find(common) != std::string::npos, "longestCommonSubstring result appears in first string");
                expectTrue(b.find(common) != std::string::npos, "longestCommonSubstring result appears in second string");
            }
        }
    }

    std::cout << (g_failures == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
