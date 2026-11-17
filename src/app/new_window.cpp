#include "app/new_window.h"
#include <utility>
#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/toplevel.h>
#include <wx/utils.h>
#include <wx/msgdlg.h>
#include <wx/translation.h>
#include <limits>
#include <string>
#include <array>
#include <vector>
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
    return LaunchWindow(false);
}

bool EditorFrame::Restart() {
    const auto& preferences = settings_ ? settings_->values : defaults_;
    if (preferences.saveBeforeRunningTools && !ConfirmDiscardChanges(_("Save changes to %s before restarting?")))
        return false;
    if (!LaunchWindow(true)) {
        wxMessageBox(_("Could not restart wxnotepad."), _("Restart failed"), wxOK | wxICON_ERROR, this);
        return false;
    }
    SaveSettings(false);
    fileChangeMonitor_.Stop();
    Destroy();
    return true;
}

OpenResult EditorFrame::OpenRestartDocument(const wxString& path, std::optional<EncodingChoice> encoding,
                                            std::optional<SyntaxLanguage> language, bool readOnly,
                                            Scintilla::Position line, Scintilla::Position column) {
    // Original Unicode/ANSI switches apply after automatic loading. Legacy -e
    // selects source decoding, including the native OEM encoding identity.
    const bool selectAfterLoad =
        encoding && !encoding->systemOem &&
        (encoding->systemDefault || encoding->encoding == FileEncoding::Utf8 ||
         encoding->encoding == FileEncoding::Utf16Le || encoding->encoding == FileEncoding::Utf16Be);
    const auto result = LoadFile(
        path, [] { return true; }, std::nullopt, std::nullopt, OpenDecisionTiming::AfterRead, false,
        selectAfterLoad ? std::nullopt : encoding);
    if (result.status != OpenStatus::Opened) return result;
    if (selectAfterLoad) document_.SelectEncoding(*encoding);
    if (language) ChooseLanguage(*language);
    if (line > 0 && column > 0) {
        const auto position =
            editor_->Send(Scintilla::Message::FindColumn, static_cast<uintptr_t>(line - 1), column - 1);
        editor_->Send(Scintilla::Message::GotoPos, static_cast<uintptr_t>(position));
        editor_->Send(Scintilla::Message::ScrollCaret);
    }
    if (readOnly) editor_->Send(Scintilla::Message::SetReadOnly, 1);
    UpdateDocumentState();
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
    return result;
}

bool EditorFrame::LaunchWindow(bool currentFile) {
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
    std::vector<wxString> arguments{executable, "--window-position", position};
    if (currentFile && !document_.Path().empty()) {
        if (editor_->Send(Scintilla::Message::GetReadOnly)) arguments.emplace_back("--read-only");
        arguments.emplace_back("--startup-encoding");
        auto encoding = document_.CurrentEncoding();
        // GetRelaunchParameters uses -utf16le/-utf16be for either UTF-16 form;
        // these original switches select their BOM formats after loading.
        if (encoding.encoding == FileEncoding::Utf16Le || encoding.encoding == FileEncoding::Utf16Be)
            encoding.bom = true;
        arguments.push_back(wxString::Format("%d", EncodingSetting(encoding)));
        arguments.emplace_back("--startup-scheme");
        arguments.push_back(
            wxString::Format("%d", static_cast<int>(std::to_underlying(document_.EffectiveLanguage()))));
        const auto caret = editor_->Send(Scintilla::Message::GetCurrentPos);
        if (caret > 0) {
            const auto line = editor_->Send(Scintilla::Message::LineFromPosition, static_cast<uintptr_t>(caret));
            const auto column = editor_->Send(Scintilla::Message::GetColumn, static_cast<uintptr_t>(caret), line);
            arguments.emplace_back("--startup-caret");
            arguments.push_back(
                wxString::Format("%lld,%lld", static_cast<long long>(line + 1), static_cast<long long>(column + 1)));
        }
        arguments.emplace_back("--startup-file");
        arguments.push_back(document_.Path());
    }
#ifdef __WXGTK__
    // wxExecute's Unix async return only confirms fork(), not a successful exec().
    // GLib confirms exec failure before returning and reaps independent children.
    std::vector<std::string> utf8;
    for (const auto& argument : arguments)
        utf8.push_back(argument.utf8_string());
    std::vector<char*> nativeArguments;
    for (auto& argument : utf8)
        nativeArguments.push_back(argument.data());
    nativeArguments.push_back(nullptr);
    return g_spawn_async(launchWorkingDirectory_.utf8_str(), nativeArguments.data(), nullptr, G_SPAWN_DEFAULT, nullptr,
                         nullptr, nullptr, nullptr) != FALSE;
#else
    std::vector<const wchar_t*> nativeArguments;
    for (const auto& argument : arguments)
        nativeArguments.push_back(argument.wc_str());
    nativeArguments.push_back(nullptr);
    wxExecuteEnv environment;
    environment.cwd = launchWorkingDirectory_;
    // The menu reports launch failures on this frame, without deferred wxLogGui dialogs.
    const wxLogNull noGuiLog;
    return wxExecute(nativeArguments.data(), wxEXEC_ASYNC, nullptr, &environment) > 0;
#endif
}
