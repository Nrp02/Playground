#include <iostream>
#include <string>
#include <vector>

#include "../src/trie.hpp"

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
        trie::Trie t;
        expectTrue(t.empty(), "new trie is empty");
        expectEq<std::size_t>(t.wordCount(), 0, "new trie has zero word count");
        expectTrue(!t.search("anything"), "empty trie search finds nothing");
        expectTrue(t.startsWith(""), "empty prefix always matches, even on empty trie");
        expectTrue(!t.startsWith("a"), "empty trie has no non-empty prefixes");
    }

    {
        trie::Trie t;
        t.insert("cat");
        expectTrue(t.search("cat"), "search finds inserted word");
        expectTrue(!t.search("ca"), "search rejects a strict prefix that was not inserted");
        expectTrue(!t.search("catalog"), "search rejects a strict extension that was not inserted");
        expectTrue(t.startsWith("c"), "startsWith matches a leading prefix");
        expectTrue(t.startsWith("ca"), "startsWith matches a longer prefix");
        expectTrue(t.startsWith("cat"), "startsWith matches the full word");
        expectTrue(!t.startsWith("cats"), "startsWith rejects a non-prefix");
        expectEq<std::size_t>(t.wordCount(), 1, "word count after one insert");
    }

    {
        trie::Trie t;
        t.insert("cat");
        t.insert("cat");
        expectEq<std::size_t>(t.wordCount(), 1, "duplicate insert does not increase word count");
    }

    {
        trie::Trie t;
        t.insert("car");
        t.insert("card");
        expectTrue(t.search("car"), "search finds the shorter word");
        expectTrue(t.search("card"), "search finds the longer word sharing a prefix");
        expectTrue(!t.search("ca"), "search rejects an unindexed prefix");
        expectTrue(t.startsWith("card"), "startsWith matches the longer word exactly");
        expectEq<std::size_t>(t.wordCount(), 2, "word count reflects both words");
    }

    {
        trie::Trie t;
        t.insert("");
        expectTrue(t.search(""), "empty string can be searched after insert");
        expectEq<std::size_t>(t.wordCount(), 1, "word count counts the empty string as a word");
        expectTrue(t.erase(""), "empty string can be erased");
        expectTrue(!t.search(""), "empty string no longer found after erase");
        expectEq<std::size_t>(t.wordCount(), 0, "word count drops after erasing empty string");
    }

    {
        trie::Trie t;
        t.insert("car");
        t.insert("card");
        expectTrue(t.erase("car"), "erase removes the shorter shared-prefix word");
        expectTrue(!t.search("car"), "erased word is no longer found");
        expectTrue(t.search("card"), "sibling word sharing the erased prefix is left intact");
        expectTrue(t.startsWith("car"), "prefix of the surviving word still matches");
        expectEq<std::size_t>(t.wordCount(), 1, "word count reflects only the surviving word");
    }

    {
        trie::Trie t;
        t.insert("cat");
        t.insert("car");
        expectTrue(t.erase("cat"), "erase removes a leaf word");
        expectTrue(!t.search("cat"), "erased leaf word is gone");
        expectTrue(!t.startsWith("cat"), "prefix unique to the erased word no longer matches");
        expectTrue(t.search("car"), "unrelated sibling word survives the erase");
        expectTrue(t.startsWith("ca"), "shared ancestor prefix still matches the surviving word");
    }

    {
        trie::Trie t;
        expectTrue(!t.erase("missing"), "erase on empty trie fails");
        t.insert("only");
        expectTrue(!t.erase("on"), "erase fails on a prefix that was never inserted as a word");
        expectTrue(t.startsWith("on"), "startsWith still matches the untouched prefix");
        expectTrue(t.erase("only"), "erase succeeds on the actual word");
        expectTrue(!t.erase("only"), "erasing the same word twice fails the second time");
        expectTrue(t.empty(), "trie is empty after erasing its only word");
    }

    {
        trie::Trie t;
        for (const auto& word : {"do", "dog", "dodge"}) {
            t.insert(word);
        }
        auto results = t.autocomplete("do", 10);
        std::vector<std::string> expected = {"do", "dodge", "dog"};
        expectEq(results, expected, "autocomplete returns all matches in DFS order");
    }

    {
        trie::Trie t;
        for (const auto& word : {"do", "dog", "dodge"}) {
            t.insert(word);
        }
        auto results = t.autocomplete("do", 2);
        std::vector<std::string> expected = {"do", "dodge"};
        expectEq(results, expected, "autocomplete respects the k limit");
    }

    {
        trie::Trie t;
        t.insert("cat");
        auto results = t.autocomplete("cat", 0);
        expectTrue(results.empty(), "autocomplete with k=0 returns nothing");
    }

    {
        trie::Trie t;
        t.insert("cat");
        auto results = t.autocomplete("dog", 5);
        expectTrue(results.empty(), "autocomplete on an absent prefix returns nothing");
    }

    {
        trie::Trie t;
        t.insert("a");
        auto results = t.autocomplete("", 10);
        std::vector<std::string> expected = {"a"};
        expectEq(results, expected, "autocomplete with empty prefix searches the whole trie");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
