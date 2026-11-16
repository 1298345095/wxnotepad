#pragma once

#include <wx/wx.h>
#include <cstdio>
#include <initializer_list>

namespace NativeFailure {
inline void Capture(const char* scenario, std::initializer_list<long> processes) {
#ifdef __WXMSW__
    const auto output = wxString::Format("Testing/Temporary/failure-evidence/%s-%lu", scenario, wxGetProcessId());
    wxString command = "\"" + wxString::FromUTF8(NATIVE_FAILURE_PYTHON) + "\" -X utf8 \"" +
                       wxString::FromUTF8(NATIVE_FAILURE_SCRIPT) + "\" --output \"" + output + "\"";
    for (const auto process : processes)
        command += wxString::Format(" --pid %ld", process);
    const auto result = wxExecute(command, wxEXEC_SYNC | wxEXEC_NOEVENTS | wxEXEC_HIDE_CONSOLE);
    std::fprintf(stderr, "Owned native failure capture: exit=%ld output=%s\n", result, output.utf8_string().c_str());
#else
    (void)scenario;
    (void)processes;
#endif
}
} // namespace NativeFailure
