#pragma once

#include <wx/wx.h>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace NativeFailure {
inline void Capture(const char* scenario, std::initializer_list<long> processes) {
#ifdef __WXMSW__
    const auto output = wxString::Format("Testing/Temporary/failure-evidence/%s-%lu", scenario, wxGetProcessId());
    wxString command = "\"" + wxString::FromUTF8(NATIVE_FAILURE_PYTHON) + "\" -X utf8 \"" +
                       wxString::FromUTF8(NATIVE_FAILURE_SCRIPT) + "\" --output \"" + output + "\"";
    for (const auto process : processes)
        command += wxString::Format(" --pid %ld", process);
    const auto foreground = ::GetForegroundWindow();
    const auto active = ::GetActiveWindow();
    const auto focus = ::GetFocus();
    std::fprintf(stderr, "Before capture helper launch: foreground=%p active=%p focus=%p\n",
                 static_cast<void*>(foreground), static_cast<void*>(active), static_cast<void*>(focus));
    command += wxString::Format(" --before-foreground %llu --before-active %llu --before-focus %llu",
                                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(foreground)),
                                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(active)),
                                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(focus)));
    const auto result = wxExecute(command, wxEXEC_SYNC | wxEXEC_NOEVENTS | wxEXEC_HIDE_CONSOLE | wxEXEC_NODISABLE);
    std::fprintf(stderr, "Owned native failure capture: exit=%ld output=%s\n", result, output.utf8_string().c_str());
#else
    (void)scenario;
    (void)processes;
#endif
}
} // namespace NativeFailure
