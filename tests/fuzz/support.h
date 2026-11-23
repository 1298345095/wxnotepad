#pragma once

#include "diagnostics/print.h"
#include <wx/init.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace Fuzz {
[[noreturn]] inline void Fail(const char* requirement) {
    DiagnosticPrint::Print(stderr, "Fuzz invariant failed: {}\n", requirement);
    std::abort();
}
inline void Check(bool condition, const char* requirement) {
    if (!condition) Fail(requirement);
}
inline void InitializeWx() {
    static const wxInitializer initialized;
    Check(initialized.IsOk(), "wxBase initialization succeeds without a display");
}
inline std::string_view Bytes(const uint8_t* data, size_t size) {
    return {reinterpret_cast<const char*>(data), size};
}
} // namespace Fuzz
