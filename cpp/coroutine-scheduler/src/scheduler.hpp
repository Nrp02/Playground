#pragma once

#include <coroutine>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <queue>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "task.hpp"

namespace cosched {

class Scheduler;

struct TimerFireState {
    std::coroutine_handle<> handle;
    bool fired = false;
    bool cancelled = false;
};

struct CancelState {
    std::shared_ptr<TimerFireState> timer;
    Scheduler* sched = nullptr;
    bool cancelledFlag = false;
};

struct TaskCancelled : std::exception {
    const char* what() const noexcept override { return "task cancelled"; }
};

class CancellationToken {
public:
    CancellationToken() : state_(std::make_shared<CancelState>()) {}

    bool cancelled() const { return state_->cancelledFlag; }

    void cancel();

    std::shared_ptr<CancelState> state_;
};

class Scheduler {
public:
    void schedule(std::coroutine_handle<> h) {
        if (h) {
            readyQueue_.push_back(h);
        }
    }

    struct SleepAwaiter {
        Scheduler& sched;
        std::uint64_t delayMs;
        std::shared_ptr<CancelState> cancelState;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) {
            auto timerState = std::make_shared<TimerFireState>();
            timerState->handle = h;
            sched.addTimer(sched.now_ + delayMs, timerState);
            if (cancelState) {
                cancelState->timer = timerState;
                cancelState->sched = &sched;
            }
        }

        void await_resume() const {
            if (cancelState && cancelState->cancelledFlag) {
                throw TaskCancelled{};
            }
        }
    };

    SleepAwaiter sleep(std::uint64_t ms) { return SleepAwaiter{*this, ms, nullptr}; }

    SleepAwaiter sleep(std::uint64_t ms, CancellationToken& token) {
        return SleepAwaiter{*this, ms, token.state_};
    }

    std::uint64_t now() const { return now_; }

    void run() {
        while (true) {
            while (!readyQueue_.empty()) {
                auto h = readyQueue_.front();
                readyQueue_.pop_front();
                if (h && !h.done()) {
                    h.resume();
                }
            }
            if (timers_.empty()) {
                break;
            }
            now_ = timers_.top().deadline;
            while (!timers_.empty() && timers_.top().deadline == now_) {
                auto entry = timers_.top();
                timers_.pop();
                if (!entry.state->fired) {
                    entry.state->fired = true;
                    if (!entry.state->cancelled) {
                        readyQueue_.push_back(entry.state->handle);
                    }
                }
            }
        }
    }

    void addTimer(std::uint64_t deadline, std::shared_ptr<TimerFireState> state) {
        timers_.push(TimerEntry{deadline, seq_++, std::move(state)});
    }

private:
    struct TimerEntry {
        std::uint64_t deadline;
        std::uint64_t seq;
        std::shared_ptr<TimerFireState> state;

        bool operator>(const TimerEntry& other) const {
            if (deadline != other.deadline) {
                return deadline > other.deadline;
            }
            return seq > other.seq;
        }
    };

    std::deque<std::coroutine_handle<>> readyQueue_;
    std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<>> timers_;
    std::uint64_t now_ = 0;
    std::uint64_t seq_ = 0;
};

inline void CancellationToken::cancel() {
    state_->cancelledFlag = true;
    auto timer = state_->timer;
    if (timer && !timer->fired) {
        timer->fired = true;
        timer->cancelled = true;
        if (state_->sched) {
            state_->sched->schedule(timer->handle);
        }
    }
}

struct Counter {
    std::size_t remaining = 0;
    std::coroutine_handle<> waiter;
    bool woken = false;
};

inline void wakeCounter(const std::shared_ptr<Counter>& counter, Scheduler* sched) {
    if (counter->waiter && !counter->woken) {
        counter->woken = true;
        sched->schedule(counter->waiter);
    }
}

struct CounterZeroAwaiter {
    std::shared_ptr<Counter> counter;

    bool await_ready() const noexcept { return counter->remaining == 0; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        counter->waiter = h;
        counter->woken = false;
    }
    void await_resume() const noexcept {}
};

struct FirstDoneAwaiter {
    std::shared_ptr<Counter> counter;
    std::shared_ptr<std::optional<std::size_t>> winner;

    bool await_ready() const noexcept { return winner->has_value(); }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        counter->waiter = h;
        counter->woken = false;
    }
    void await_resume() const noexcept {}
};

struct VoidResult {};

template <typename T>
using WhenAllElement = std::conditional_t<std::is_void_v<T>, VoidResult, T>;

template <typename T>
WhenAllElement<T> extractWhenAllResult(Task<T>& t) {
    if constexpr (std::is_void_v<T>) {
        t.result();
        return VoidResult{};
    } else {
        return t.result();
    }
}

template <typename T>
void startCountedSub(Scheduler& sched, Task<T>& t, const std::shared_ptr<Counter>& counter) {
    Scheduler* schedPtr = &sched;
    t.onFinalize([counter, schedPtr]() {
        --counter->remaining;
        if (counter->remaining == 0) {
            wakeCounter(counter, schedPtr);
        }
    });
    sched.schedule(t.handle());
}

template <typename... Ts>
Task<std::tuple<WhenAllElement<Ts>...>> when_all(Scheduler& sched, Task<Ts>... tasks) {
    auto counter = std::make_shared<Counter>();
    counter->remaining = sizeof...(Ts);
    (startCountedSub(sched, tasks, counter), ...);
    co_await CounterZeroAwaiter{counter};
    co_return std::tuple<WhenAllElement<Ts>...>(extractWhenAllResult(tasks)...);
}

template <typename T>
Task<T> when_any(Scheduler& sched, std::vector<Task<T>> tasks, std::vector<CancellationToken> tokens) {
    auto counter = std::make_shared<Counter>();
    counter->remaining = tasks.size();
    auto winner = std::make_shared<std::optional<std::size_t>>();

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        Scheduler* schedPtr = &sched;
        tasks[i].onFinalize([counter, winner, i, schedPtr]() {
            if (!winner->has_value()) {
                *winner = i;
                wakeCounter(counter, schedPtr);
            }
            --counter->remaining;
            if (counter->remaining == 0) {
                wakeCounter(counter, schedPtr);
            }
        });
        sched.schedule(tasks[i].handle());
    }

    co_await FirstDoneAwaiter{counter, winner};
    std::size_t winnerIndex = **winner;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        if (i != winnerIndex) {
            tokens[i].cancel();
        }
    }

    co_await CounterZeroAwaiter{counter};
    co_return tasks[winnerIndex].result();
}

}
