#include "app/new_window.h"
#include "app/editor_frame.h"
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/toplevel.h>
#include <wx/utils.h>
#include <limits>
#include <string>
#include <array>
#ifdef __WXMSW__
#include <windows.h>
#elif defined(__WXGTK__)
#include <glib.h>
#endif

bool ApplyInitialWindowPosition(wxTopLevelWindow& window, const wxString& argument) {
    const auto parts = wxSplit(argument, ',');
    std::array<int, 5> values{};
    if (parts.size() != values.size()) return false;
    for (size_t i = 0; i < parts.size(); ++i) {
        long value = 0;
        if (!parts[i].ToLong(&value) || value < std::numeric_limits<int>::min() ||
            value > std::numeric_limits<int>::max())
            return false;
        values[i] = static_cast<int>(value);
    }
    const auto [x, y, width, height, maximized] = values;
    if (width <= 0 || height <= 0 || (maximized != 0 && maximized != 1)) return false;
#ifdef __WXMSW__
    window.SetSize(x, y, width, height);
#else
    window.SetSize(width, height);
#endif
    window.Maximize(maximized != 0);
    return true;
}

bool EditorFrame::NewWindow() {
    wxRect normal(wxPoint(0, 0), normalWindowSize_);
#ifdef __WXMSW__
    // Original Notepad4::GetRelaunchParameters(NewWindow | EmptyWindow).
    constexpr int CascadeOffsetPixels = 10;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(static_cast<HWND>(GetHandle()), &placement)) return false;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfo(MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    const auto& rectangle = placement.rcNormalPosition;
    normal = wxRect(rectangle.left + CascadeOffsetPixels, rectangle.top + CascadeOffsetPixels,
                    rectangle.right - rectangle.left, rectangle.bottom - rectangle.top);
    if (normal.GetX() + normal.GetWidth() > monitor.rcWork.right ||
        normal.GetY() + normal.GetHeight() > monitor.rcWork.bottom)
        normal.SetPosition(wxPoint(monitor.rcMonitor.left, monitor.rcMonitor.top));
#endif
    const wxString executable = wxStandardPaths::Get().GetExecutablePath();
    const wxString position = wxString::Format("%d,%d,%d,%d,%d", normal.GetX(), normal.GetY(), normal.GetWidth(),
                                               normal.GetHeight(), IsMaximized());
#ifdef __WXGTK__
    // wxExecute's Unix async return only confirms fork(), not a successful exec().
    // GLib confirms exec failure before returning and reaps independent children.
    std::string programPath = executable.ToStdString(wxConvUTF8);
    std::string geometry = position.ToStdString(wxConvUTF8);
    char option[] = "--window-position";
    char* arguments[] = {programPath.data(), option, geometry.data(), nullptr};
    return g_spawn_async(launchWorkingDirectory_.utf8_str(), arguments, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr,
                         nullptr, nullptr) != FALSE;
#else
    const wchar_t* arguments[] = {executable.wc_str(), L"--window-position", position.wc_str(), nullptr};
    wxExecuteEnv environment;
    environment.cwd = launchWorkingDirectory_;
    // The menu reports launch failures on this frame, without deferred wxLogGui dialogs.
    const wxLogNull noGuiLog;
    return wxExecute(arguments, wxEXEC_ASYNC, nullptr, &environment) > 0;
#endif
}
