#pragma once

#include <cstdint>
#include <cstddef>

// Notepad4 89b8b36: preserve unsupported suspend/shutdown/overwrite INI bits.
namespace AutoSaveOption {
constexpr int Periodic = 1;
constexpr int Suspend = 2;
constexpr int Shutdown = 4;
constexpr int ManuallyDelete = 8;
constexpr int OverwriteCurrent = 16;
constexpr int Default = Suspend | Shutdown;
constexpr uint32_t DefaultPeriodMilliseconds = 5000;
constexpr size_t MaximumManagedBackups = 6;
} // namespace AutoSaveOption

struct AutoSaveOptions {
    int flags = AutoSaveOption::Default;
    uint32_t periodMilliseconds = AutoSaveOption::DefaultPeriodMilliseconds;
};
