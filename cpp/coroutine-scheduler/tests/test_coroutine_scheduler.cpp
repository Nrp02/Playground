#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/scheduler.hpp"
#include "../src/task.hpp"

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

using cosched::CancellationToken;
using cosched::Scheduler;
using cosched::Task;
using cosched::TaskCancelled;
using cosched::when_all;
using cosched::when_any;

Task<int> makeValueTask(int v) { co_return v; }

Task<void> makeVoidTask(bool& flag) {
    flag = true;
    co_return;
}

Task<int> addOneLevel(Task<int> inner) {
    int v = co_await inner;
    co_return v + 1;
}

Task<int> throwsDeep() {
    co_await makeValueTask(1);
    throw std::runtime_error("deep failure");
    co_return 0;
}

Task<int> middleLayer() {
    int v = co_await throwsDeep();
    co_return v;
}

Task<int> topLayer() {
    int v = co_await middleLayer();
    co_return v;
}

Task<int> sleepAndReturn(Scheduler& sched, std::uint64_t ms, int value, std::vector<int>& order) {
    co_await sched.sleep(ms);
    order.push_back(value);
    co_return value;
}

Task<void> runSleepOrderTest(Scheduler& sched, std::vector<int>& order) {
    auto a = sleepAndReturn(sched, 30, 1, order);
    auto b = sleepAndReturn(sched, 10, 2, order);
    auto c = sleepAndReturn(sched, 20, 3, order);
    auto d = sleepAndReturn(sched, 10, 4, order);
    co_await when_all(sched, std::move(a), std::move(b), std::move(c), std::move(d));
}

Task<void> runWhenAllTest(Scheduler& sched, int& sum, bool& voidRan) {
    auto [x, y, z] = co_await when_all(sched, makeValueTask(10), makeValueTask(20),
                                        makeVoidTask(voidRan));
    sum = x + y;
    (void)z;
}

Task<int> raceSleeper(Scheduler& sched, std::uint64_t ms, CancellationToken token) {
    co_await sched.sleep(ms, token);
    co_return static_cast<int>(ms);
}

Task<void> runWhenAnyTest(Scheduler& sched, int& winner) {
    std::vector<Task<int>> tasks;
    std::vector<CancellationToken> tokens;
    tokens.emplace_back();
    tokens.emplace_back();
    tokens.emplace_back();
    tasks.push_back(raceSleeper(sched, 50, tokens[0]));
    tasks.push_back(raceSleeper(sched, 5, tokens[1]));
    tasks.push_back(raceSleeper(sched, 30, tokens[2]));
    winner = co_await when_any(sched, std::move(tasks), std::move(tokens));
}

Task<void> cancellableTask(Scheduler& sched, CancellationToken token, bool& cleanupRan,
                            bool& sawCancelled) {
    struct Guard {
        bool& flag;
        ~Guard() { flag = true; }
    } guard{cleanupRan};
    try {
        co_await sched.sleep(1000, token);
    } catch (const TaskCancelled&) {
        sawCancelled = true;
        throw;
    }
}

Task<void> driveCancellation(Scheduler& sched, bool& cleanupRan, bool& sawCancelled,
                              bool& propagated) {
    CancellationToken token;
    auto t = cancellableTask(sched, token, cleanupRan, sawCancelled);
    sched.schedule(t.handle());
    co_await sched.sleep(5);
    token.cancel();
    co_await sched.sleep(5);
    try {
        t.result();
    } catch (const TaskCancelled&) {
        propagated = true;
    }
}

}

int main() {
    {
        Scheduler sched;
        auto t = makeValueTask(42);
        sched.schedule(t.handle());
        sched.run();
        expectEq(t.result(), 42, "value task returns its result");
    }

    {
        Scheduler sched;
        bool ran = false;
        auto t = makeVoidTask(ran);
        sched.schedule(t.handle());
        sched.run();
        expectTrue(ran, "void task body executes");
        t.result();
        expectTrue(true, "void task result() does not throw");
    }

    {
        Scheduler sched;
        auto t = addOneLevel(makeValueTask(9));
        sched.schedule(t.handle());
        sched.run();
        expectEq(t.result(), 10, "nested co_await chain propagates result");
    }

    {
        Scheduler sched;
        auto t = topLayer();
        sched.schedule(t.handle());
        sched.run();
        bool threw = false;
        std::string message;
        try {
            t.result();
        } catch (const std::runtime_error& e) {
            threw = true;
            message = e.what();
        }
        expectTrue(threw, "exception thrown deep in chain surfaces at top-level await");
        expectEq(message, std::string("deep failure"), "exception message preserved across co_await boundaries");
    }

    {
        Scheduler sched;
        std::vector<int> order;
        auto t = runSleepOrderTest(sched, order);
        sched.schedule(t.handle());
        sched.run();
        std::vector<int> expected{2, 4, 3, 1};
        expectTrue(order == expected, "timers resume in deadline order with stable ties");
    }

    {
        Scheduler sched;
        int sum = 0;
        bool voidRan = false;
        auto t = runWhenAllTest(sched, sum, voidRan);
        sched.schedule(t.handle());
        sched.run();
        expectEq(sum, 30, "when_all aggregates results from multiple value tasks");
        expectTrue(voidRan, "when_all also drives a void task to completion");
    }

    {
        Scheduler sched;
        int winner = -1;
        auto t = runWhenAnyTest(sched, winner);
        sched.schedule(t.handle());
        sched.run();
        expectEq(winner, 5, "when_any resolves to the fastest task");
    }

    {
        Scheduler sched;
        bool cleanupRan = false;
        bool sawCancelled = false;
        bool propagated = false;
        auto t = driveCancellation(sched, cleanupRan, sawCancelled, propagated);
        sched.schedule(t.handle());
        sched.run();
        expectTrue(sawCancelled, "cancellation wakes a sleeping task early");
        expectTrue(cleanupRan, "cancellation unwind runs RAII cleanup");
        expectTrue(propagated, "TaskCancelled propagates out to the joining caller");
    }

    {
        int before = cosched::frameCount();
        {
            Scheduler sched;
            std::vector<int> order;
            auto t = runSleepOrderTest(sched, order);
            sched.schedule(t.handle());
            sched.run();
        }
        expectEq(cosched::frameCount(), before, "no coroutine frames leak after scheduler drains");
    }

    {
        Scheduler sched;
        int winner = -1;
        {
            auto t = runWhenAnyTest(sched, winner);
            sched.schedule(t.handle());
            sched.run();
        }
        expectEq(cosched::frameCount(), 0, "when_any leaves zero live frames once fully drained");
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
