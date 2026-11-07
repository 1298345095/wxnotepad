#include "steady_stopwatch.h"
#include <chrono>
#include <cstdio>
#include <sys/time.h>
#include <time.h>

namespace {
int wallOffset = 0;
}

extern "C" int __real_clock_gettime(clockid_t clock, timespec* value);
extern "C" int __real_gettimeofday(timeval* value, void* zone);
extern "C" int __wrap_clock_gettime(clockid_t clock, timespec* value) {
    const int result = __real_clock_gettime(clock, value);
    if (result == 0 && clock == CLOCK_REALTIME) value->tv_sec += wallOffset;
    return result;
}
extern "C" int __wrap_gettimeofday(timeval* value, void* zone) {
    const int result = __real_gettimeofday(value, zone);
    if (result == 0) value->tv_sec += wallOffset;
    return result;
}

int main() {
    using Clock = std::chrono::steady_clock;
    timeval before{}, after{};
    if (gettimeofday(&before, nullptr) != 0) return 1;
    auto startedBefore = Clock::now();
    TestClock::StopWatch watch;
    auto startedAfter = Clock::now();
    const auto matchesSteadyClock = [&] {
        const auto readBefore = Clock::now();
        const auto elapsed = watch.Time();
        const auto readAfter = Clock::now();
        return elapsed >= std::chrono::duration_cast<std::chrono::milliseconds>(readBefore - startedAfter).count() &&
               elapsed <= std::chrono::duration_cast<std::chrono::milliseconds>(readAfter - startedBefore).count();
    };
    wallOffset = 3600;
    if (gettimeofday(&after, nullptr) != 0 || after.tv_sec - before.tv_sec < 3599) return 1;
    if (!matchesSteadyClock()) {
        std::fputs("FAIL: forward wall-clock correction expires a native polling budget\n", stderr);
        return 1;
    }
    startedBefore = Clock::now();
    watch.Start();
    startedAfter = Clock::now();
    wallOffset = -3600;
    if (gettimeofday(&after, nullptr) != 0 || before.tv_sec - after.tv_sec < 3599) return 1;
    if (!matchesSteadyClock()) {
        std::fputs("FAIL: backward wall-clock correction changes a native polling budget\n", stderr);
        return 1;
    }
    std::puts("PASS: UTC advances and rewinds preserve native polling budgets");
}
