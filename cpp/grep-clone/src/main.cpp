#include <iostream>
#include <string>
#include <vector>

#include "grep.hpp"

int main() {
    std::vector<std::string> buffer = {
        "the quick brown fox jumps over the lazy dog",
        "log level=INFO message=starting server on port 8080",
        "log level=ERROR message=connection refused",
        "cat and dog and bird",
        "banana",
        "color: gray, colour: grey",
        "func add(a, b) { return a + b; }",
        "func subtract(a, b) { return a - b; }",
        "127.0.0.1 localhost",
        "no digits here at all",
        "phone: 555-1234",
        "start of line anchor test",
        "this line ends with cat",
        "aaa",
        "abbbc",
        "abc",
        "ac",
    };

    std::vector<std::pair<std::string, std::string>> patterns = {
        {"dog", "literal"},
        {"c.t", "dot wildcard"},
        {"ab*c", "star quantifier"},
        {"ab+c", "plus quantifier"},
        {"colou?r", "optional quantifier"},
        {"[0-9]+", "character class range"},
        {"b[^a]*d", "negated character class"},
        {"^log", "start anchor"},
        {"cat$", "end anchor"},
        {"(cat|dog)", "alternation"},
        {"(a|b)(c|d)", "grouping with alternation"},
        {"^[0-9]+[.]", "anchored digits followed by a literal dot"},
    };

    for (const auto& [pattern, label] : patterns) {
        std::cout << "=== pattern: " << pattern << "  (" << label << ") ===\n";
        auto matches = rex::grep(pattern, buffer);
        if (matches.empty()) {
            std::cout << "  (no matches)\n";
        }
        for (const auto& [lineNo, line] : matches) {
            std::cout << "  " << lineNo << ":" << line << "\n";
        }
    }

    return 0;
}
