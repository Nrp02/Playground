#include <iostream>
#include <stdexcept>

#include "circuit_breaker.hpp"

namespace {

int flakyDownstream(int callIndex) {
    if (callIndex >= 5 && callIndex < 25) {
        throw std::runtime_error("downstream unavailable");
    }
    return callIndex;
}

}

int main() {
    cb::CircuitBreakerConfig config;
    config.rollingWindowSize = 8;
    config.minimumCalls = 4;
    config.failureThreshold = 0.5;
    config.cooldownMs = 3000;
    config.halfOpenMaxCalls = 2;
    config.halfOpenSuccessThreshold = 2;

    cb::CircuitBreaker breaker(config);
    breaker.setStateChangeListener([](cb::State from, cb::State to, std::int64_t nowMs) {
        std::cout << "[t=" << nowMs << "ms] state change: " << cb::toString(from) << " -> "
                  << cb::toString(to) << "\n";
    });

    std::int64_t nowMs = 0;
    for (int callIndex = 0; callIndex < 40; ++callIndex) {
        nowMs = callIndex * 500;
        std::cout << "[t=" << nowMs << "ms] call " << callIndex << " state="
                  << cb::toString(breaker.state()) << " -> ";
        try {
            int result = breaker.call([callIndex]() { return flakyDownstream(callIndex); }, nowMs);
            std::cout << "success (result=" << result << ")\n";
        } catch (const cb::CircuitOpenError&) {
            std::cout << "short-circuited (breaker open)\n";
        } catch (const std::runtime_error& e) {
            std::cout << "downstream failed: " << e.what() << "\n";
        }
    }

    return 0;
}
