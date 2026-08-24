// Minimal, dependency-free test harness for src/protocol.{hpp,cpp}. No
// external test framework is pulled in on purpose: the whole project
// builds with nothing but a C++17 compiler and `make`, and a handful of
// pure functions don't need more than plain assertions to be well tested.
//
// Run via `make test` from cpp/chat-server/.

#include <iostream>
#include <string>

#include "../src/protocol.hpp"

namespace {

int g_failures = 0;

void expectEq(const std::string& actual, const std::string& expected, const std::string& testName) {
    if (actual != expected) {
        std::cerr << "FAIL: " << testName << "\n  expected: \"" << expected << "\"\n  actual:   \"" << actual
                   << "\"\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << '\n';
}

void expectTrue(bool condition, const std::string& testName) {
    if (!condition) {
        std::cerr << "FAIL: " << testName << '\n';
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << '\n';
}

}  // namespace

int main() {
    using chat::protocol::formatBroadcastLine;
    using chat::protocol::trim;

    expectEq(trim("  hello  "), "hello", "trim strips surrounding spaces");
    expectEq(trim("\t\r\nhello world\r\n"), "hello world", "trim strips tabs/CR/LF but keeps inner space");
    expectEq(trim(""), "", "trim of an empty string is empty");
    expectEq(trim("   \t  "), "", "trim of an all-whitespace string is empty");
    expectEq(trim("no-surrounding-whitespace"), "no-surrounding-whitespace", "trim is a no-op when unnecessary");
    expectEq(trim("/quit"), "/quit", "trim preserves command strings like /quit");

    expectEq(formatBroadcastLine("hi there", "Alice"), "[Alice] hi there\n", "formats a normal chat message");
    expectEq(formatBroadcastLine("Alice has joined the chat.", ""), "Alice has joined the chat.\n",
              "system messages (empty sender) get no [name] prefix");
    expectEq(formatBroadcastLine("already terminated\n", "Bob"), "[Bob] already terminated\n",
              "does not double an already-present trailing newline");
    expectEq(formatBroadcastLine("", "Carol"), "[Carol] \n", "empty message body still gets a prefix and newline");

    std::string ts = chat::protocol::timestamp();
    expectTrue(ts.size() == 8 && ts[2] == ':' && ts[5] == ':',
               "timestamp() formats as HH:MM:SS (got \"" + ts + "\")");

    if (g_failures == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cerr << '\n' << g_failures << " test(s) failed.\n";
    return 1;
}
