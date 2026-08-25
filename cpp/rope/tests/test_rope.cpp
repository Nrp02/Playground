#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

#include "../src/rope.hpp"

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
        rope::Rope r;
        expectTrue(r.empty(), "default rope is empty");
        expectEq<std::size_t>(r.length(), 0, "default rope length is zero");
        expectEq(r.toString(), std::string(""), "default rope toString is empty");
    }

    {
        rope::Rope r("hello world");
        expectEq<std::size_t>(r.length(), 11, "constructed rope has correct length");
        expectEq(r.toString(), std::string("hello world"), "toString matches constructor input");
        expectEq(r.charAt(0), 'h', "charAt(0) correct");
        expectEq(r.charAt(6), 'w', "charAt(6) correct");
        expectEq(r.charAt(10), 'd', "charAt last index correct");
    }

    {
        rope::Rope r("hello world");
        expectEq(r.substring(0, 5), std::string("hello"), "substring prefix correct");
        expectEq(r.substring(6, 11), std::string("world"), "substring suffix correct");
        expectEq(r.substring(0, 11), std::string("hello world"), "substring full range correct");
        expectEq(r.substring(3, 3), std::string(""), "substring empty range correct");
    }

    {
        rope::Rope r("helloworld");
        r.insert(5, " ");
        expectEq(r.toString(), std::string("hello world"), "insert in middle correct");
        r.insert(0, ">>");
        expectEq(r.toString(), std::string(">>hello world"), "insert at start correct");
        r.append("!!");
        expectEq(r.toString(), std::string(">>hello world!!"), "append at end correct");
    }

    {
        rope::Rope r("hello world");
        r.erase(5, 1);
        expectEq(r.toString(), std::string("helloworld"), "erase middle char correct");
        r.erase(0, 5);
        expectEq(r.toString(), std::string("world"), "erase prefix correct");
    }

    {
        rope::Rope a("hello ");
        rope::Rope b("world");
        rope::Rope c = rope::Rope::concat(a, b);
        expectEq(c.toString(), std::string("hello world"), "static concat combines ropes");
        expectEq<std::size_t>(c.length(), 11, "concat length correct");
        expectEq<std::size_t>(a.length(), 6, "concat leaves left operand unchanged");
        expectEq<std::size_t>(b.length(), 5, "concat leaves right operand unchanged");
    }

    {
        bool threw = false;
        rope::Rope r("abc");
        try {
            r.charAt(10);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        expectTrue(threw, "charAt out of range throws");
    }

    {
        bool threw = false;
        rope::Rope r("abc");
        try {
            r.insert(10, "x");
        } catch (const std::out_of_range&) {
            threw = true;
        }
        expectTrue(threw, "insert out of range throws");
    }

    {
        bool threw = false;
        rope::Rope r("abc");
        try {
            r.erase(1, 10);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        expectTrue(threw, "erase out of range throws");
    }

    {
        bool threw = false;
        rope::Rope r("abc");
        try {
            r.substring(2, 1);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        expectTrue(threw, "substring with start > end throws");
    }

    {
        std::string big(5000, 'x');
        rope::Rope r(big);
        expectEq<std::size_t>(r.length(), 5000, "large rope construction has correct length");
        expectEq(r.toString(), big, "large rope toString matches source");
        expectTrue(r.treeHeight() < 40, "large balanced rope has logarithmic-ish height");
    }

    {
        std::mt19937 rng(99);
        std::string reference;
        rope::Rope r;
        for (int i = 0; i < 400; ++i) {
            int op = static_cast<int>(rng() % 4);
            if (op == 0 || reference.empty()) {
                std::size_t pos = reference.empty() ? 0 : rng() % (reference.size() + 1);
                std::size_t chunkLen = 1 + rng() % 12;
                std::string chunk;
                for (std::size_t j = 0; j < chunkLen; ++j) {
                    chunk += static_cast<char>('a' + rng() % 26);
                }
                reference.insert(pos, chunk);
                r.insert(pos, chunk);
            } else if (op == 1) {
                std::size_t pos = rng() % reference.size();
                std::size_t maxCount = reference.size() - pos;
                std::size_t count = 1 + rng() % maxCount;
                reference.erase(pos, count);
                r.erase(pos, count);
            } else if (op == 2) {
                std::size_t pos = rng() % reference.size();
                expectEq(r.charAt(pos), reference[pos], "randomized charAt matches reference");
            } else {
                std::size_t lo = rng() % reference.size();
                std::size_t hi = lo + rng() % (reference.size() - lo + 1);
                expectEq(r.substring(lo, hi), reference.substr(lo, hi - lo),
                         "randomized substring matches reference");
            }
        }
        expectEq(r.toString(), reference, "final rope matches reference string after randomized ops");
        expectEq(r.length(), reference.size(), "final rope length matches reference size");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
