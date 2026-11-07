#include "steady_stopwatch.h"
#include <chrono>
#include <cstdio>
#include <sys/time.h>
#include <time.h>

namespace {
int wallOffset = 0;
}

// GNU ld's ABI spellings remain linker labels rather than reserved C++ names.
extern "C" int RealClockGetTime(clockid_t clock, timespec* value) asm("__real_clock_gettime");
extern "C" int RealGetTimeOfDay(timeval* value, void* zone) asm("__real_gettimeofday");
extern "C" int WrapClockGetTime(clockid_t clock, timespec* value) asm("__wrap_clock_gettime");
extern "C" int WrapGetTimeOfDay(timeval* value, void* zone) asm("__wrap_gettimeofday");
extern "C" int WrapClockGetTime(clockid_t clock, timespec* value) {
    const int result = RealClockGetTime(clock, value);
    if (result == 0 && clock == CLOCK_REALTIME) value->tv_sec += wallOffset;
    return result;
}
extern "C" int WrapGetTimeOfDay(timeval* value, void* zone) {
    const int result = RealGetTimeOfDay(value, zone);
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
