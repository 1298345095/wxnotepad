// PROTOTYPE ONLY: phase counters and ETW markers, never linked into the normal host.
#pragma once
#include <windows.h>
#include <evntrace.h>
#include <evntprov.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace DWriteDiagnosis {
enum class Metric : std::uint8_t {
    CoreResize,
    CoreChangeSize,
    CoreWrap,
    CorePaint,
    TargetCreate,
    TargetResize,
    Present,
    PixmapCreate,
    DrawText,
    LayoutCreate,
    MeasureWidths,
    WidthText,
    Count
};
constexpr std::array Names{"core_resize",   "core_change_size",   "core_wrap_all",  "core_paint",
                           "target_create", "target_resize",      "end_draw",       "pixmap_create",
                           "draw_text",     "create_text_layout", "measure_widths", "width_text"};
static_assert(Names.size() == static_cast<size_t>(Metric::Count));
struct Counter {
    std::atomic<std::uint64_t> calls{0}, workerCalls{0}, nanoseconds{0};
};
inline std::array<Counter, Names.size()> counters;
inline std::atomic<bool> enabled{false};
inline DWORD guiThread = 0;
inline REGHANDLE provider = 0;
inline bool captureMemory = true;
// {0F0BE3AA-4C34-4F96-A363-6C15FF2B3619}; private diagnostic marker provider.
inline constexpr GUID ProviderId{0x0f0be3aa, 0x4c34, 0x4f96, {0xa3, 0x63, 0x6c, 0x15, 0xff, 0x2b, 0x36, 0x19}};
inline std::uint64_t Now() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
class Scope {
    Counter& counter_;
    const bool active_ = enabled.load(std::memory_order_relaxed);
    const std::uint64_t start_ = active_ ? Now() : 0;

public:
    explicit Scope(Metric metric) noexcept : counter_(counters[static_cast<size_t>(metric)]) {}
    ~Scope() {
        if (!active_) return;
        counter_.nanoseconds.fetch_add(Now() - start_, std::memory_order_relaxed);
        counter_.calls.fetch_add(1, std::memory_order_relaxed);
        if (GetCurrentThreadId() != guiThread) counter_.workerCalls.fetch_add(1, std::memory_order_relaxed);
    }
};
inline bool Initialize() noexcept {
    guiThread = GetCurrentThreadId();
    const auto status = EventRegister(&ProviderId, nullptr, nullptr, &provider);
    if (status != ERROR_SUCCESS) std::fprintf(stderr, "ETW marker registration failed: %lu\n", status);
    return status == ERROR_SUCCESS;
}
inline void Marker(const char* kind, const char* name) noexcept {
    wchar_t marker[256]{};
    std::swprintf(marker, std::size(marker), L"[DEBUG-DWRITE-DIAG] %hs %hs", kind, name);
    EventWriteString(provider, TRACE_LEVEL_INFORMATION, 1, marker);
    LARGE_INTEGER qpc{}, frequency{};
    FILETIME utc{};
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&frequency);
    GetSystemTimePreciseAsFileTime(&utc);
    const auto filetime = (std::uint64_t(utc.dwHighDateTime) << 32U) | utc.dwLowDateTime;
    std::printf("{\"diagnostic_marker\":\"%s\",\"name\":\"%s\",\"pid\":%lu,\"qpc\":%lld,\"qpc_frequency\":%lld,\"utc_"
                "filetime\":%llu}\n",
                kind, name, GetCurrentProcessId(), qpc.QuadPart, frequency.QuadPart,
                static_cast<unsigned long long>(filetime));
}
inline void Begin(const char* phase) noexcept {
    for (auto& counter : counters) {
        counter.calls.store(0, std::memory_order_relaxed);
        counter.workerCalls.store(0, std::memory_order_relaxed);
        counter.nanoseconds.store(0, std::memory_order_relaxed);
    }
    Marker("begin", phase);
    enabled.store(true, std::memory_order_relaxed);
}
inline void End(const char* phase) noexcept {
    enabled.store(false, std::memory_order_relaxed);
    Marker("end", phase);
    for (size_t index = 0; index < counters.size(); ++index) {
        const auto& counter = counters[index];
        std::printf("{\"diagnostic_counter\":\"%s\",\"phase_name\":\"%s\",\"calls\":%llu,\"worker_calls\":%llu,\"sum_"
                    "ns\":%llu}\n",
                    Names[index], phase, static_cast<unsigned long long>(counter.calls.load()),
                    static_cast<unsigned long long>(counter.workerCalls.load()),
                    static_cast<unsigned long long>(counter.nanoseconds.load()));
    }
    std::fflush(stdout);
}
inline void JsonString(const char* value) noexcept {
    std::putchar('"');
    for (; *value; ++value) {
        if (*value == '\\' || *value == '"') std::putchar('\\');
        std::putchar(*value);
    }
    std::putchar('"');
}
inline void Memory(const char* stage) noexcept {
    if (!captureMemory) return;
    Marker("memory", stage);
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    const bool countersOk =
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory)) != 0;
    std::uint64_t privateCommit = 0, imageCommit = 0, mappedCommit = 0;
    MEMORY_BASIC_INFORMATION region{};
    std::uintptr_t address = 0;
    while (VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))) {
        if (region.State == MEM_COMMIT) {
            if (region.Type == MEM_PRIVATE) privateCommit += region.RegionSize;
            if (region.Type == MEM_IMAGE) imageCommit += region.RegionSize;
            if (region.Type == MEM_MAPPED) mappedCommit += region.RegionSize;
        }
        const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (next <= address) break;
        address = next;
    }
    constexpr size_t ModuleCapacity = 512;
    std::array<HMODULE, ModuleCapacity> modules{};
    DWORD needed = 0;
    const bool modulesOk = EnumProcessModules(GetCurrentProcess(), modules.data(), sizeof(modules), &needed) != 0;
    std::printf("{\"diagnostic_memory\":\"%s\",\"counters_ok\":%s,\"private_bytes\":%llu,\"rss_bytes\":%llu,\"private_"
                "virtual_commit\":%llu,\"image_virtual_commit\":%llu,\"mapped_virtual_commit\":%llu,\"modules_"
                "complete\":%s,\"module_names\":[",
                stage, countersOk ? "true" : "false", static_cast<unsigned long long>(memory.PrivateUsage),
                static_cast<unsigned long long>(memory.WorkingSetSize), static_cast<unsigned long long>(privateCommit),
                static_cast<unsigned long long>(imageCommit), static_cast<unsigned long long>(mappedCommit),
                modulesOk && needed <= sizeof(modules) ? "true" : "false");
    if (modulesOk) {
        const auto count = std::min(modules.size(), needed / sizeof(HMODULE));
        for (size_t index = 0; index < count; ++index) {
            char name[MAX_PATH]{};
            GetModuleBaseNameA(GetCurrentProcess(), modules[index], name, std::size(name));
            if (index) std::putchar(',');
            JsonString(name);
        }
    }
    std::puts("]}");
    std::fflush(stdout);
}
inline void Finish() noexcept {
    EventUnregister(provider);
    provider = 0;
}
} // namespace DWriteDiagnosis
