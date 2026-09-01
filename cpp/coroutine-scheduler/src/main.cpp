#include <cstdio>
#include <string>
#include <vector>

#include "scheduler.hpp"
#include "task.hpp"

using cosched::CancellationToken;
using cosched::Scheduler;
using cosched::Task;
using cosched::TaskCancelled;
using cosched::when_all;
using cosched::when_any;

namespace {

Task<int> simulatedRequest(Scheduler& sched, std::string name, std::vector<int> ioDelaysMs) {
    int total = 0;
    for (int delay : ioDelaysMs) {
        std::printf("[t=%llu] %s: starting io step (%dms)\n",
                    static_cast<unsigned long long>(sched.now()), name.c_str(), delay);
        co_await sched.sleep(static_cast<std::uint64_t>(delay));
        total += delay;
        std::printf("[t=%llu] %s: io step complete\n",
                    static_cast<unsigned long long>(sched.now()), name.c_str());
    }
    std::printf("[t=%llu] %s: request finished, total=%d\n",
                static_cast<unsigned long long>(sched.now()), name.c_str(), total);
    co_return total;
}

Task<int> racer(Scheduler& sched, std::string name, int delayMs, CancellationToken token) {
    struct CleanupTracer {
        std::string label;
        Scheduler* sched;
        ~CleanupTracer() {
            std::printf("[t=%llu] %s: cleanup ran\n",
                        static_cast<unsigned long long>(sched->now()), label.c_str());
        }
    } tracer{name, &sched};
    co_await sched.sleep(static_cast<std::uint64_t>(delayMs), token);
    std::printf("[t=%llu] %s: racer finished\n",
                static_cast<unsigned long long>(sched.now()), name.c_str());
    co_return delayMs;
}

Task<void> cancellableSleeper(Scheduler& sched, CancellationToken token) {
    struct CleanupTracer {
        Scheduler* sched;
        ~CleanupTracer() {
            std::printf("[t=%llu] cancellable-sleeper: cleanup ran during unwind\n",
                        static_cast<unsigned long long>(sched->now()));
        }
    } tracer{&sched};
    std::printf("[t=%llu] cancellable-sleeper: going to sleep for 500ms\n",
                static_cast<unsigned long long>(sched.now()));
    co_await sched.sleep(500, token);
    std::printf("[t=%llu] cancellable-sleeper: woke up normally\n",
                static_cast<unsigned long long>(sched.now()));
}

Task<void> driveCancellationDemo(Scheduler& sched) {
    CancellationToken token;
    auto sleeper = cancellableSleeper(sched, token);
    sched.schedule(sleeper.handle());
    co_await sched.sleep(50);
    std::printf("[t=%llu] main: cancelling the sleeper early\n",
                static_cast<unsigned long long>(sched.now()));
    token.cancel();
    co_await sched.sleep(10);
    try {
        sleeper.result();
        std::printf("cancellable-sleeper: did not throw (unexpected)\n");
    } catch (const TaskCancelled&) {
        std::printf("cancellable-sleeper: TaskCancelled propagated to caller as expected\n");
    }
}

Task<void> driveWhenAll(Scheduler& sched) {
    auto [a, b, c] = co_await when_all(sched,
                                        simulatedRequest(sched, "req-A", {30, 20}),
                                        simulatedRequest(sched, "req-B", {10}),
                                        simulatedRequest(sched, "req-C", {40, 5, 5}));
    std::printf("when_all results: A=%d B=%d C=%d\n", a, b, c);
}

Task<void> driveWhenAny(Scheduler& sched) {
    std::vector<Task<int>> racers;
    std::vector<CancellationToken> tokens;
    tokens.emplace_back();
    tokens.emplace_back();
    tokens.emplace_back();
    racers.push_back(racer(sched, "racer-slow", 200, tokens[0]));
    racers.push_back(racer(sched, "racer-fast", 40, tokens[1]));
    racers.push_back(racer(sched, "racer-medium", 100, tokens[2]));

    int winnerDelay = co_await when_any(sched, std::move(racers), std::move(tokens));
    std::printf("when_any winner delay=%d\n", winnerDelay);
}

Task<void> runDemo(Scheduler& sched) {
    std::printf("=== interleaved concurrent requests ===\n");
    auto r1 = simulatedRequest(sched, "req-1", {15, 25});
    auto r2 = simulatedRequest(sched, "req-2", {5, 5, 5});
    auto r3 = simulatedRequest(sched, "req-3", {50});
    co_await when_all(sched, std::move(r1), std::move(r2), std::move(r3));

    std::printf("\n=== when_all demo (aggregated results) ===\n");
    co_await driveWhenAll(sched);

    std::printf("\n=== when_any demo ===\n");
    co_await driveWhenAny(sched);

    std::printf("\n=== cancellation demo ===\n");
    co_await driveCancellationDemo(sched);
}

}

int main() {
    Scheduler sched;
    {
        auto demo = runDemo(sched);
        sched.schedule(demo.handle());
        sched.run();
    }
    std::printf("\nfinal frame count (should be 0): %d\n", cosched::frameCount());
    return 0;
}
