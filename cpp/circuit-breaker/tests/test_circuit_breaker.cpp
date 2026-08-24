#include <iostream>
#include <stdexcept>
#include <string>

#include "../src/circuit_breaker.hpp"

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

int succeed() { return 1; }

int failCall() { throw std::runtime_error("downstream boom"); }

}

int main() {
    {
        cb::CircuitBreakerConfig config;
        config.rollingWindowSize = 4;
        config.minimumCalls = 4;
        config.failureThreshold = 0.5;
        cb::CircuitBreaker breaker(config);

        breaker.call([]() { return succeed(); }, 0);
        breaker.call([]() { return succeed(); }, 1);
        expectEq(breaker.state(), cb::State::Closed, "stays closed after successes only");

        bool tripped = false;
        for (int i = 2; i <= 4; ++i) {
            try {
                breaker.call([]() -> int { return failCall(); }, i);
            } catch (const std::runtime_error&) {
            }
            if (breaker.state() == cb::State::Open) {
                tripped = true;
            }
        }
        expectTrue(tripped, "trips to open once failure rate exceeds threshold");
        expectEq(breaker.state(), cb::State::Open, "state is open after tripping");
    }

    {
        cb::CircuitBreakerConfig config;
        config.rollingWindowSize = 2;
        config.minimumCalls = 2;
        config.failureThreshold = 0.5;
        config.cooldownMs = 1000;
        cb::CircuitBreaker breaker(config);

        for (int i = 0; i < 2; ++i) {
            try {
                breaker.call([]() -> int { return failCall(); }, i);
            } catch (const std::runtime_error&) {
            }
        }
        expectEq(breaker.state(), cb::State::Open, "breaker opens after two failures");

        int downstreamCalls = 0;
        bool sawCircuitOpenError = false;
        try {
            breaker.call(
                [&downstreamCalls]() -> int {
                    ++downstreamCalls;
                    return succeed();
                },
                100);
        } catch (const cb::CircuitOpenError&) {
            sawCircuitOpenError = true;
        }
        expectTrue(sawCircuitOpenError, "call while open throws CircuitOpenError");
        expectEq(downstreamCalls, 0, "downstream is not invoked while open (fail fast)");
    }

    {
        cb::CircuitBreakerConfig config;
        config.rollingWindowSize = 2;
        config.minimumCalls = 2;
        config.failureThreshold = 0.5;
        config.cooldownMs = 1000;
        config.halfOpenMaxCalls = 1;
        config.halfOpenSuccessThreshold = 1;
        cb::CircuitBreaker breaker(config);

        for (int i = 0; i < 2; ++i) {
            try {
                breaker.call([]() -> int { return failCall(); }, i);
            } catch (const std::runtime_error&) {
            }
        }
        expectEq(breaker.state(), cb::State::Open, "breaker opens before cooldown test");

        try {
            breaker.call([]() -> int { return succeed(); }, 500);
        } catch (const cb::CircuitOpenError&) {
        }
        expectEq(breaker.state(), cb::State::Open, "still open before cooldown elapses");

        int downstreamCalls = 0;
        int result = breaker.call(
            [&downstreamCalls]() -> int {
                ++downstreamCalls;
                return succeed();
            },
            1001);
        expectEq(downstreamCalls, 1, "probe call reaches downstream after cooldown elapses");
        expectEq(result, 1, "probe call returns downstream result");
        expectEq(breaker.state(), cb::State::Closed,
                  "single successful half-open probe closes the breaker");
    }

    {
        cb::CircuitBreakerConfig config;
        config.rollingWindowSize = 2;
        config.minimumCalls = 2;
        config.failureThreshold = 0.5;
        config.cooldownMs = 1000;
        config.halfOpenMaxCalls = 1;
        config.halfOpenSuccessThreshold = 1;
        cb::CircuitBreaker breaker(config);

        for (int i = 0; i < 2; ++i) {
            try {
                breaker.call([]() -> int { return failCall(); }, i);
            } catch (const std::runtime_error&) {
            }
        }
        expectEq(breaker.state(), cb::State::Open, "breaker opens before half-open failure test");

        try {
            breaker.call([]() -> int { return failCall(); }, 1001);
        } catch (const std::runtime_error&) {
        }
        expectEq(breaker.state(), cb::State::Open,
                  "failed half-open probe trips breaker back to open");

        try {
            breaker.call([]() -> int { return succeed(); }, 1500);
        } catch (const cb::CircuitOpenError&) {
        }
        expectEq(breaker.state(), cb::State::Open,
                  "still open immediately after reopening (cooldown restarted)");

        int result = breaker.call([]() -> int { return succeed(); }, 2002);
        expectEq(result, 1, "second probe after restarted cooldown reaches downstream");
        expectEq(breaker.state(), cb::State::Closed,
                  "successful probe after restarted cooldown closes the breaker");
    }

    {
        cb::CircuitBreakerConfig config;
        config.rollingWindowSize = 4;
        config.minimumCalls = 4;
        config.failureThreshold = 0.5;
        config.cooldownMs = 500;
        config.halfOpenMaxCalls = 2;
        config.halfOpenSuccessThreshold = 2;
        cb::CircuitBreaker breaker(config);

        for (int i = 0; i < 4; ++i) {
            try {
                breaker.call([i]() -> int {
                    if (i < 3) {
                        return failCall();
                    }
                    return succeed();
                }, i);
            } catch (const std::runtime_error&) {
            }
        }
        expectEq(breaker.state(), cb::State::Open,
                  "failure rate above threshold trips breaker open");

        breaker.call([]() -> int { return succeed(); }, 503);
        expectEq(breaker.state(), cb::State::HalfOpen,
                  "one success below threshold keeps breaker half-open");

        breaker.call([]() -> int { return succeed(); }, 600);
        expectEq(breaker.state(), cb::State::Closed,
                  "second success reaches half-open success threshold and closes breaker");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
