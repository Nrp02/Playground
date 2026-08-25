#include <iostream>
#include <string>
#include <vector>

#include "../src/grep.hpp"
#include "../src/regex_engine.hpp"

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

}

int main() {
    {
        rex::Regex re("cat");
        expectTrue(re.fullMatch("cat"), "literal full match");
        expectTrue(!re.fullMatch("cats"), "literal full match rejects extra suffix");
        expectTrue(!re.fullMatch("dog"), "literal full match rejects mismatch");
        expectTrue(re.search("a cat sat"), "literal search finds substring");
        expectTrue(!re.search("a dog sat"), "literal search rejects absent substring");
    }

    {
        rex::Regex re("c.t");
        expectTrue(re.fullMatch("cat"), "dot wildcard matches any single char");
        expectTrue(re.fullMatch("c#t"), "dot wildcard matches symbol");
        expectTrue(!re.fullMatch("ct"), "dot wildcard requires exactly one char");
        expectTrue(!re.fullMatch("caat"), "dot wildcard does not match two chars");
    }

    {
        rex::Regex re("ab*c");
        expectTrue(re.fullMatch("ac"), "star quantifier matches zero repetitions");
        expectTrue(re.fullMatch("abc"), "star quantifier matches one repetition");
        expectTrue(re.fullMatch("abbbbc"), "star quantifier matches many repetitions");
        expectTrue(!re.fullMatch("adc"), "star quantifier rejects wrong middle char");
    }

    {
        rex::Regex re("ab+c");
        expectTrue(!re.fullMatch("ac"), "plus quantifier requires at least one repetition");
        expectTrue(re.fullMatch("abc"), "plus quantifier matches one repetition");
        expectTrue(re.fullMatch("abbbc"), "plus quantifier matches many repetitions");
    }

    {
        rex::Regex re("ab?c");
        expectTrue(re.fullMatch("ac"), "quest quantifier matches zero occurrences");
        expectTrue(re.fullMatch("abc"), "quest quantifier matches one occurrence");
        expectTrue(!re.fullMatch("abbc"), "quest quantifier rejects two occurrences");
    }

    {
        rex::Regex re("a*");
        expectTrue(re.fullMatch(""), "star on empty string matches zero-width");
        expectTrue(re.fullMatch("aaaa"), "star matches many repetitions from empty base");
    }

    {
        rex::Regex re("[abc]");
        expectTrue(re.fullMatch("a"), "class matches member a");
        expectTrue(re.fullMatch("b"), "class matches member b");
        expectTrue(!re.fullMatch("d"), "class rejects non-member");
    }

    {
        rex::Regex re("[a-z]+");
        expectTrue(re.fullMatch("hello"), "class range matches lowercase run");
        expectTrue(!re.fullMatch("Hello"), "class range rejects uppercase char");
    }

    {
        rex::Regex re("[^0-9]+");
        expectTrue(re.fullMatch("hello"), "negated class matches non-digits");
        expectTrue(!re.fullMatch("hell0"), "negated class rejects string containing digit");
    }

    {
        rex::Regex re("^abc");
        expectTrue(re.search("abcdef"), "start anchor matches at beginning");
        expectTrue(!re.search("xabcdef"), "start anchor rejects match not at beginning");
    }

    {
        rex::Regex re("abc$");
        expectTrue(re.search("xxabc"), "end anchor matches at end");
        expectTrue(!re.search("abcxx"), "end anchor rejects match not at end");
    }

    {
        rex::Regex re("^abc$");
        expectTrue(re.fullMatch("abc"), "both anchors match exact string");
        expectTrue(re.search("abc"), "both anchors search matches exact string");
        expectTrue(!re.search("xabc"), "both anchors reject prefixed string");
        expectTrue(!re.search("abcx"), "both anchors reject suffixed string");
    }

    {
        rex::Regex re("cat|dog");
        expectTrue(re.fullMatch("cat"), "alternation matches first branch");
        expectTrue(re.fullMatch("dog"), "alternation matches second branch");
        expectTrue(!re.fullMatch("bird"), "alternation rejects neither branch");
        expectTrue(re.search("I have a dog"), "alternation search finds second branch substring");
    }

    {
        rex::Regex re("(a|b)(c|d)");
        expectTrue(re.fullMatch("ac"), "grouped alternation matches ac");
        expectTrue(re.fullMatch("ad"), "grouped alternation matches ad");
        expectTrue(re.fullMatch("bc"), "grouped alternation matches bc");
        expectTrue(re.fullMatch("bd"), "grouped alternation matches bd");
        expectTrue(!re.fullMatch("aa"), "grouped alternation rejects invalid combination");
    }

    {
        rex::Regex re("(ab)+c");
        expectTrue(re.fullMatch("ababc"), "nested group with plus matches repeated group");
        expectTrue(re.fullMatch("abc"), "nested group with plus matches single group");
        expectTrue(!re.fullMatch("ac"), "nested group with plus rejects zero groups");
    }

    {
        rex::Regex re("(a(b|c)d)*");
        expectTrue(re.fullMatch(""), "deeply nested group star matches empty string");
        expectTrue(re.fullMatch("abdacd"), "deeply nested group star matches repeated nested groups");
        expectTrue(!re.fullMatch("abdax"), "deeply nested group star rejects malformed tail");
    }

    {
        std::vector<std::string> lines = {
            "the quick brown fox",
            "jumps over the lazy dog",
            "pack my box with five dozen liquor jugs",
            "no matching content on this line",
        };
        auto matches = rex::grep("fox|dog", lines);
        expectEq<std::size_t>(matches.size(), 2, "grep finds both matching lines");
        expectEq(matches[0].first, 1, "grep reports correct line number for first match");
        expectEq(matches[1].first, 2, "grep reports correct line number for second match");
        expectEq(matches[0].second, lines[0], "grep returns the original line text");
    }

    {
        std::vector<std::string> lines = {"apple", "banana", "cherry"};
        auto matches = rex::grep("[0-9]+", lines);
        expectTrue(matches.empty(), "grep returns no matches when pattern absent from all lines");
    }

    {
        std::vector<std::string> lines = {
            "level=INFO starting up",
            "level=ERROR connection refused",
            "level=WARN retrying",
        };
        auto matches = rex::grep("^level=ERROR", lines);
        expectEq<std::size_t>(matches.size(), 1, "grep with anchor finds exactly one line");
        expectEq(matches[0].first, 2, "grep with anchor reports correct line number");
    }

    std::cout << "\n" << g_failures << " failing test(s)\n";
    return g_failures == 0 ? 0 : 1;
}
