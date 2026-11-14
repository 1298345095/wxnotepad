#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

inline unsigned ResearchRequestedThreads = 0;
inline unsigned ResearchActualThreads = 0;
inline std::thread::id ResearchGuiThread;
enum ResearchMetric { Calls, WorkerCalls, Bytes, CallNs, WaitNs, HeldNs, NativeNs, FlushNs, MetricCount };
inline std::array<std::atomic<std::uint64_t>, MetricCount> ResearchStats{};
inline std::uint64_t ResearchNow() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
inline std::uint64_t ResearchCpu() {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        throw std::runtime_error("CPU counter");
    const auto value = [](FILETIME t) { return (std::uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return (value(kernel) + value(user)) * 100;
#else
    timespec value{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value)) throw std::runtime_error("CPU counter");
    return std::uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
#endif
}
inline void ResearchAdd(ResearchMetric metric, std::uint64_t value) {
    ResearchStats[metric].fetch_add(value, std::memory_order_relaxed);
}
inline void ResearchReset() {
    for (auto& value : ResearchStats)
        value.store(0, std::memory_order_relaxed);
}
class ResearchCall {
    const std::uint64_t start_ = ResearchNow();

public:
    explicit ResearchCall(size_t bytes) {
        ResearchAdd(Calls, 1);
        ResearchAdd(Bytes, bytes);
        if (std::this_thread::get_id() != ResearchGuiThread) ResearchAdd(WorkerCalls, 1);
    }
    ~ResearchCall() { ResearchAdd(CallNs, ResearchNow() - start_); }
};
class ResearchLock {
    const std::uint64_t begin_ = ResearchNow();
    std::unique_lock<std::mutex> lock_;
    const std::uint64_t acquired_;

public:
    explicit ResearchLock(std::mutex& mutex) : lock_(mutex), acquired_(ResearchNow()) {
        ResearchAdd(WaitNs, acquired_ - begin_);
    }
    ~ResearchLock() { ResearchAdd(HeldNs, ResearchNow() - acquired_); }
};
class ResearchNativeScope {
    const ResearchMetric metric_;
    const std::uint64_t start_ = ResearchNow();

public:
    explicit ResearchNativeScope(ResearchMetric metric) : metric_(metric) {}
    ~ResearchNativeScope() { ResearchAdd(metric_, ResearchNow() - start_); }
};
inline void ResearchReport(const char* fixture, const char* phase, std::uint64_t wall, std::uint64_t cpu) {
    std::printf("{\"fixture\":\"%s\",\"phase\":\"%s\",\"threads\":%u,\"wall_ns\":%llu,\"cpu_ns\":%llu", fixture, phase,
                ResearchActualThreads, static_cast<unsigned long long>(wall), static_cast<unsigned long long>(cpu));
    constexpr std::array names{"calls",   "worker_calls", "bytes",     "call_ns",
                               "wait_ns", "held_ns",      "native_ns", "flush_ns"};
    for (size_t i = 0; i < names.size(); ++i)
        std::printf(",\"%s\":%llu", names[i], static_cast<unsigned long long>(ResearchStats[i].load()));
    std::puts("}");
    std::fflush(stdout);
}
