#pragma once

// Evidence-only recorder, shared by diagnostic copies of the exact 7f sources.
// The GUI thread records fixed-size numeric entries; no measured-path I/O.
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <stdexcept>

namespace PeriodicTrace {
enum class Event {
    ContinuousBegin,
    ContinuousEnd,
    AppendBegin,
    AppendEnd,
    PumpBegin,
    PumpEnd,
    DocumentChanged,
    TimerRequest,
    TimerStart,
    TimerStarted,
    TimerStop,
    BackupBegin,
    BackupSkipped,
    BackupWritten,
    CreateBegin,
    CreateEnd,
    WriteBegin,
    WriteEnd,
    CloseBegin,
    CloseEnd,
};
struct Entry {
    int64_t us;
    Event event;
    std::array<uint64_t, 5> values;
};
inline constexpr size_t Capacity = 2048;
inline std::array<Entry, Capacity> entries;
inline size_t count = 0;
inline size_t dropped = 0;
inline bool enabled = false;
inline bool dumped = false;
inline std::filesystem::path output;
inline std::filesystem::path root;
inline std::array<wchar_t, 32768> fallback;
inline size_t fallbackLength = 0;
inline std::wstring processSuffix;
inline std::chrono::steady_clock::time_point started;

inline uint64_t NameKey(std::wstring_view value) {
    constexpr uint64_t FnvOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t FnvPrime = 1099511628211ULL;
    uint64_t key = FnvOffsetBasis;
    for (const auto unit : value) {
        key ^= static_cast<uint64_t>(unit);
        key *= FnvPrime;
    }
    return key;
}
inline void Configure(const std::filesystem::path& ownedRoot, unsigned long process) {
    const auto* directory = std::getenv("WXNOTEPAD_PERIODIC_TRACE_DIR");
    if (!directory || !*directory) return;
    output = directory;
    root = ownedRoot;
    processSuffix = std::to_wstring(process);
    started = std::chrono::steady_clock::now();
    enabled = true;
}
inline void ObserveFallback(std::wstring_view directory) {
    if (!enabled || directory.size() >= fallback.size()) return;
    std::copy_n(directory.begin(), directory.size(), fallback.begin());
    fallbackLength = directory.size();
}
inline void Record(Event event, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0, uint64_t e = 0) {
    if (!enabled) return;
    if (count == Capacity) {
        ++dropped;
        return;
    }
    entries[count++] = {
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count(),
        event,
        {a, b, c, d, e}};
}
inline void Dump(const char* outcome) {
    if (!enabled || dumped) return;
    dumped = true;
    // Post-outcome evidence only: the original assertion has already evaluated.
    try {
        std::filesystem::create_directories(output);
        const auto filename = output / "periodic-trace.tsv";
        auto* stream = _wfopen(filename.c_str(), L"wb");
        if (!stream) throw std::runtime_error("open owned trace output");
        std::fprintf(stream, "outcome=%s count=%zu dropped=%zu capacity=%zu\n", outcome, count, dropped, Capacity);
        std::fputs("us\tevent\ta\tb\tc\td\te\n", stream);
        for (size_t index = 0; index < count; ++index) {
            const auto& entry = entries[index];
            std::fprintf(
                stream, "%lld\t%d\t%llu\t%llu\t%llu\t%llu\t%llu\n", static_cast<long long>(entry.us),
                static_cast<int>(entry.event), static_cast<unsigned long long>(entry.values[0]),
                static_cast<unsigned long long>(entry.values[1]), static_cast<unsigned long long>(entry.values[2]),
                static_cast<unsigned long long>(entry.values[3]), static_cast<unsigned long long>(entry.values[4]));
        }
        std::fclose(stream);
        std::filesystem::copy(root, output / "owned-root", std::filesystem::copy_options::recursive);
        const std::filesystem::path fallbackPath(std::wstring_view(fallback.data(), fallbackLength));
        if (fallbackLength && std::filesystem::is_directory(fallbackPath)) {
            for (const auto& file : std::filesystem::directory_iterator(fallbackPath)) {
                const auto name = file.path().filename().wstring();
                if (file.is_regular_file() &&
                    (name.ends_with(processSuffix + L".txt") || name.ends_with(processSuffix + L".bak"))) {
                    std::filesystem::create_directories(output / "owned-fallback");
                    std::filesystem::copy_file(file.path(), output / "owned-fallback" / file.path().filename());
                }
            }
        }
        std::fprintf(stderr, "Periodic diagnostic trace retained: %s\n", filename.string().c_str());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Periodic diagnostic evidence error: %s\n", error.what());
    }
}
} // namespace PeriodicTrace
