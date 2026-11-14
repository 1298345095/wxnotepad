#pragma once
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

// Resource checkpoints are outside every timed layout interval.
inline void PrototypeMemory(const char* fixture, const char* stage) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters)))
        throw std::runtime_error("process memory counters");
    std::printf("{\"metric\":\"memory\",\"fixture\":\"%s\",\"stage\":\"%s\",\"private_bytes\":%llu,\"rss_bytes\":%llu,"
                "\"peak_rss_bytes\":%llu,\"gdi_objects\":%lu}\n",
                fixture, stage, static_cast<unsigned long long>(counters.PrivateUsage),
                static_cast<unsigned long long>(counters.WorkingSetSize),
                static_cast<unsigned long long>(counters.PeakWorkingSetSize),
                GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
#else
    unsigned long long rss = 0, peak = 0;
    std::ifstream status("/proc/self/status");
    std::string key, line;
    while (status >> key) {
        if (key == "VmRSS:") status >> rss;
        if (key == "VmHWM:") status >> peak;
        std::getline(status, line);
    }
    if (!rss || !peak) throw std::runtime_error("process memory counters");
    std::printf(
        "{\"metric\":\"memory\",\"fixture\":\"%s\",\"stage\":\"%s\",\"rss_bytes\":%llu,\"peak_rss_bytes\":%llu}\n",
        fixture, stage, rss * 1024, peak * 1024);
#endif
    std::fflush(stdout);
}
