#include <chrono>
#include <iostream>
#include <random>
#include <string>

#include "rope.hpp"

int main() {
    const std::size_t initialSize = 20000000;
    std::mt19937 seedRng(42);
    std::string base(initialSize, 'a');
    for (auto& c : base) {
        c = static_cast<char>('a' + (seedRng() % 26));
    }

    rope::Rope textRope(base);
    std::string textString(base);

    std::cout << "initial length: " << textRope.length() << "\n";
    std::cout << "initial tree height: " << textRope.treeHeight() << "\n";

    const int insertions = 3000;
    const std::size_t frontWindow = 1000;
    const std::string chunk = "0123456789";

    std::cout << insertions << " inserts, each at a random position within the first "
              << frontWindow << " characters of a " << initialSize
              << "-character text (worst case for std::string, since every insert must shift\n"
              << "almost the entire remaining buffer; a rope only touches O(log n) tree nodes):\n";

    std::mt19937 ropeRng(123);
    std::mt19937 stringRng(123);

    auto ropeStart = std::chrono::steady_clock::now();
    for (int i = 0; i < insertions; ++i) {
        std::size_t pos = ropeRng() % frontWindow;
        textRope.insert(pos, chunk);
    }
    auto ropeEnd = std::chrono::steady_clock::now();

    auto stringStart = std::chrono::steady_clock::now();
    for (int i = 0; i < insertions; ++i) {
        std::size_t pos = stringRng() % frontWindow;
        textString.insert(pos, chunk);
    }
    auto stringEnd = std::chrono::steady_clock::now();

    double ropeMs = std::chrono::duration<double, std::milli>(ropeEnd - ropeStart).count();
    double stringMs = std::chrono::duration<double, std::milli>(stringEnd - stringStart).count();

    std::cout << "  rope:        " << ropeMs << " ms\n";
    std::cout << "  std::string: " << stringMs << " ms\n";
    if (ropeMs > 0.0) {
        std::cout << "  speedup: " << (stringMs / ropeMs) << "x\n";
    }

    std::cout << "final rope length: " << textRope.length() << "\n";
    std::cout << "final rope tree height: " << textRope.treeHeight() << "\n";
    std::cout << "rope/std::string content match: " << (textRope.toString() == textString) << "\n";

    rope::Rope greeting("hello ");
    rope::Rope subject("world");
    rope::Rope combined = rope::Rope::concat(greeting, subject);
    std::cout << "concat demo: " << combined.toString() << "\n";

    std::string snippet = textRope.substring(1000, 1040);
    std::cout << "substring[1000,1040): " << snippet << "\n";
    std::cout << "charAt(0): " << textRope.charAt(0) << "\n";

    textRope.erase(500, 100);
    std::cout << "length after erase(500, 100): " << textRope.length() << "\n";

    return 0;
}
