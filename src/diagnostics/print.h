#pragma once

#include <cstdio>
#include <exception>
#include <format>
#include <print>
#include <string_view>

namespace DiagnosticPrint {
// Error reporting must leave the caller's retry, cleanup or abort path intact.
inline bool Write(FILE* stream, std::string_view format, std::format_args arguments) noexcept {
    try {
        std::vprint_unicode(stream, format, arguments);
        return true;
    } catch (const std::exception& error) {
        std::fputs("Diagnostic output failed: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
        return false;
    }
}

template <typename... Args> bool Print(FILE* stream, std::format_string<Args...> format, Args&&... arguments) noexcept {
    return Write(stream, format.get(), std::make_format_args(arguments...));
}
} // namespace DiagnosticPrint
