#pragma once

#ifdef WXNOTEPAD_DIAGNOSTICS
#include <cstdint>

namespace DiagnosticTrace {
bool Enabled() noexcept;
void Write(const char* event, const char* format = "", ...) noexcept;
std::uint64_t BeginGesture() noexcept;
void EndGesture() noexcept;
std::uint64_t Gesture() noexcept;
} // namespace DiagnosticTrace

// Arguments, including state queries and formatting, are evaluated only while
// tracing is enabled. An ordinary build removes the whole expression.
#define WXNOTE_TRACE(...)                                                                                              \
    do {                                                                                                               \
        if (DiagnosticTrace::Enabled()) DiagnosticTrace::Write(__VA_ARGS__);                                           \
    } while (false)
#else
#define WXNOTE_TRACE(...)                                                                                              \
    do {                                                                                                               \
    } while (false)
#endif
