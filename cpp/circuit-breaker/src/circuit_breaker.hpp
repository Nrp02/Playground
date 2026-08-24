#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace cb {

enum class State { Closed, Open, HalfOpen };

inline std::string toString(State state) {
    switch (state) {
        case State::Closed:
            return "CLOSED";
        case State::Open:
            return "OPEN";
        case State::HalfOpen:
            return "HALF_OPEN";
    }
    return "UNKNOWN";
}

class CircuitOpenError : public std::runtime_error {
public:
    CircuitOpenError() : std::runtime_error("circuit breaker is open") {}
};

struct CircuitBreakerConfig {
    std::size_t rollingWindowSize = 10;
    std::size_t minimumCalls = 5;
    double failureThreshold = 0.5;
    std::int64_t cooldownMs = 5000;
    std::size_t halfOpenMaxCalls = 1;
    std::size_t halfOpenSuccessThreshold = 1;
};

class CircuitBreaker {
public:
    using StateChangeListener = std::function<void(State, State, std::int64_t)>;

    explicit CircuitBreaker(CircuitBreakerConfig config = CircuitBreakerConfig())
        : config_(config) {}

    void setStateChangeListener(StateChangeListener listener) {
        listener_ = std::move(listener);
    }

    template <typename Func>
    auto call(Func&& func, std::int64_t nowMs) -> decltype(func()) {
        applyCooldownIfElapsed(nowMs);

        if (state_ == State::Open) {
            throw CircuitOpenError();
        }
        if (state_ == State::HalfOpen) {
            if (halfOpenCallsIssued_ >= config_.halfOpenMaxCalls) {
                throw CircuitOpenError();
            }
            ++halfOpenCallsIssued_;
        }

        using ResultType = decltype(func());
        if constexpr (std::is_void_v<ResultType>) {
            try {
                func();
            } catch (...) {
                recordFailure(nowMs);
                throw;
            }
            recordSuccess(nowMs);
        } else {
            try {
                ResultType result = func();
                recordSuccess(nowMs);
                return result;
            } catch (...) {
                recordFailure(nowMs);
                throw;
            }
        }
    }

    State state() const { return state_; }

    std::size_t rollingCallCount() const { return window_.size(); }

    std::size_t rollingFailureCount() const {
        std::size_t failures = 0;
        for (bool ok : window_) {
            if (!ok) {
                ++failures;
            }
        }
        return failures;
    }

    void reset(std::int64_t nowMs) { transitionTo(State::Closed, nowMs); }

private:
    void applyCooldownIfElapsed(std::int64_t nowMs) {
        if (state_ == State::Open && nowMs - openedAtMs_ >= config_.cooldownMs) {
            transitionTo(State::HalfOpen, nowMs);
        }
    }

    void recordSuccess(std::int64_t nowMs) {
        if (state_ == State::HalfOpen) {
            ++halfOpenSuccesses_;
            if (halfOpenSuccesses_ >= config_.halfOpenSuccessThreshold) {
                transitionTo(State::Closed, nowMs);
            }
            return;
        }
        pushResult(true);
        evaluateTripCondition(nowMs);
    }

    void recordFailure(std::int64_t nowMs) {
        if (state_ == State::HalfOpen) {
            transitionTo(State::Open, nowMs);
            return;
        }
        pushResult(false);
        evaluateTripCondition(nowMs);
    }

    void evaluateTripCondition(std::int64_t nowMs) {
        if (window_.size() < config_.minimumCalls) {
            return;
        }
        double failureRate =
            static_cast<double>(rollingFailureCount()) / static_cast<double>(window_.size());
        if (failureRate > config_.failureThreshold) {
            transitionTo(State::Open, nowMs);
        }
    }

    void pushResult(bool success) {
        window_.push_back(success);
        while (window_.size() > config_.rollingWindowSize) {
            window_.pop_front();
        }
    }

    void transitionTo(State next, std::int64_t nowMs) {
        State previous = state_;
        state_ = next;
        if (next == State::Open) {
            openedAtMs_ = nowMs;
            halfOpenCallsIssued_ = 0;
            halfOpenSuccesses_ = 0;
        } else if (next == State::HalfOpen) {
            halfOpenCallsIssued_ = 0;
            halfOpenSuccesses_ = 0;
        } else if (next == State::Closed) {
            window_.clear();
            halfOpenCallsIssued_ = 0;
            halfOpenSuccesses_ = 0;
        }
        if (previous != next && listener_) {
            listener_(previous, next, nowMs);
        }
    }

    CircuitBreakerConfig config_;
    State state_ = State::Closed;
    std::deque<bool> window_;
    std::int64_t openedAtMs_ = 0;
    std::size_t halfOpenCallsIssued_ = 0;
    std::size_t halfOpenSuccesses_ = 0;
    StateChangeListener listener_;
};

}
