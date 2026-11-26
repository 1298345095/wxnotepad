#include "app/new_window.h"
#include <utility>
#include "app/editor_frame.h"
#include "app/native_launch.h"
#include "app/window_routing.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include <wx/stdpaths.h>
#include <wx/toplevel.h>
#include <wx/msgdlg.h>
#include <wx/translation.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <limits>
#include <array>
#include <vector>
#ifdef __WXMSW__
#include <windows.h>
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

OpenStatus EditorFrame::ReceiveWindowRequest(const WindowRequest& request) {
    OpenStatus status = OpenStatus::Cancelled;
    if (ConfirmDiscardChanges(_("Save changes to %s before opening another file?"))) {
        bool load = true;
        auto source = request.source;
        if (request.ordinaryFile && !wxFileExists(request.startup) && !wxDirExists(request.startup)) {
            load = wxMessageBox(
                       wxString::Format(_("\"%s\" not found.\nWould you like to create this file?"), request.startup),
                       _("Create file?"), wxYES_NO | wxCANCEL | wxICON_QUESTION, this) == wxYES;
            if (load) {
                const wxLogNull quiet;
                wxFile file;
                load = file.Create(request.startup, false) && file.Close();
                if (load && std::holds_alternative<DocumentLoad::NativeAnsiSource>(source)) {
                    if (const auto native = NativeEncodingChoice()) source = *native;
                }
                if (!load)
                    wxMessageBox(_("Could not create the file."), _("Could not open file"), wxOK | wxICON_ERROR, this);
            }
        }
        if (load) {
            const auto result = OpenRestartDocument(request.startup, request.encoding, request.scheme, request.readOnly,
                                                    request.line, request.column, source);
            status = result.status;
            if (status == OpenStatus::Failed)
                wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
        }
    }
    // Original WM_COPYDATA jumps even when Save/Discard/Open was cancelled.
    if (request.initialLocation) GoToInitialLocation(*editor_, *request.initialLocation);
    return status;
}

bool EditorFrame::OpenInNewWindow() {
    const auto& preferences = settings_ ? settings_->values : defaults_;
    if (preferences.saveBeforeRunningTools && !ConfirmDiscardChanges(_("Save changes to %s before running tools?")))
        return false;
    if (LaunchWindow(true)) return true;
    wxMessageBox(_("Could not open the document in a new window."), _("Open in New Window failed"), wxOK | wxICON_ERROR,
                 this);
    return false;
}

bool EditorFrame::Restart() {
    const auto& preferences = settings_ ? settings_->values : defaults_;
    if (preferences.saveBeforeRunningTools && !ConfirmDiscardChanges(_("Save changes to %s before restarting?")))
        return false;
    // Finish all parent settings writes before the child reads its startup INI.
    SaveSettings(false);
    if (!LaunchWindow(true)) {
        wxMessageBox(_("Could not restart wxnotepad."), _("Restart failed"), wxOK | wxICON_ERROR, this);
        return false;
    }
    fileChangeMonitor_.Stop();
    Destroy();
    return true;
}

OpenResult EditorFrame::OpenRestartDocument(const wxString& path, std::optional<EncodingChoice> encoding,
                                            std::optional<SyntaxLanguage> language, bool readOnly,
                                            Scintilla::Position line, Scintilla::Position column,
                                            DocumentLoad::SourceDecoding source) {
    // Original Unicode/ANSI switches apply after automatic loading. Legacy -e
    // selects source decoding, including the native OEM encoding identity.
    const bool explicitSource = !std::holds_alternative<std::monostate>(source);
    const bool selectAfterLoad =
        encoding && (explicitSource ||
                     (!encoding->systemOem &&
                      (encoding->systemDefault || encoding->encoding == FileEncoding::Utf8 ||
                       encoding->encoding == FileEncoding::Utf16Le || encoding->encoding == FileEncoding::Utf16Be)));
    const DocumentLoad::SourceDecoding decoding = explicitSource ? std::move(source)
                                                  : !selectAfterLoad && encoding
                                                      ? DocumentLoad::SourceDecoding(*encoding)
                                                      : DocumentLoad::SourceDecoding{};
    const auto result =
        path.empty() ? OpenResult{OpenStatus::Opened, {}} : LoadFile(DocumentLoad::Open{path, decoding});
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
    std::vector<wxString> arguments{executable, "-n", "--window-position", position};
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
    wxString error;
    return NativeLaunch::SpawnArguments(arguments, launchWorkingDirectory_, error);
}
