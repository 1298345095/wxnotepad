#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "ScintillaStructures.h"
#include "app/native_launch.h"
#include "app/desktop_launch.h"
#include "syntax/syntax_catalog.h"
#include "diagnostics/trace.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/msgdlg.h>
#include <wx/menu.h>
#include <wx/translation.h>
#include <string>
#ifdef __WXMSW__
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#elif defined(__WXGTK__)
#include <gio/gio.h>
#endif

namespace {
using Scintilla::Message;
std::string Range(EditorControl& editor, intptr_t start, intptr_t end) {
    if (end <= start) return {};
    std::string text(static_cast<size_t>(end - start) + 1, '\0');
    Scintilla::TextRangeFull range{{start, end}, text.data()};
    editor.Send(Message::GetTextRangeFull, 0, reinterpret_cast<intptr_t>(&range));
    text.pop_back();
    return text;
}
std::string SelectedTarget(EditorControl& editor) {
    // Adapted from Notepad4 89b8b36 EditGetStringAroundCaret/EditOpenSelection.
    // See licenses/Notepad4.txt. The native core always exposes UTF-8 bytes.
    const auto selectionLength = editor.Send(Message::GetSelText);
    if (selectionLength == 0) {
        const auto caret = editor.Send(Message::GetCurrentPos);
        const auto line = editor.Send(Message::LineFromPosition, caret);
        auto start = editor.Send(Message::PositionFromLine, line);
        auto end = editor.Send(Message::GetLineEndPosition, line);
        if (start == end) return {};
        const auto style = editor.Send(Message::GetStyleIndexAt, caret);
        if (editor.Send(Message::StyleGetHotSpot, style)) {
            start = caret;
            while (start > 0 && editor.Send(Message::GetStyleIndexAt, start - 1) == style)
                --start;
            end = caret;
            const auto length = editor.Send(Message::GetLength);
            while (end < length && editor.Send(Message::GetStyleIndexAt, end) == style)
                ++end;
            return Range(editor, start, end);
        }
        constexpr auto flags = static_cast<uintptr_t>(Scintilla::FindOption::RegExp | Scintilla::FindOption::Posix);
        Scintilla::TextToFindFull find{{caret, end}, "[\\s'`\"<>|*,;]", 0, {0, 0}};
        if (caret < end && editor.Send(Message::FindTextFull, flags, reinterpret_cast<intptr_t>(&find)) >= 0) {
            const auto position = find.chrgText.cpMax;
            const auto ch = editor.Send(Message::GetCharAt, position);
            end = editor.Send(Message::GetCharAt, position - 1) == ',' && ch >= '0' && ch <= '9'
                      ? editor.Send(Message::WordEndPosition, position, true)
                      : editor.Send(Message::PositionBefore, position);
        }
        find.chrg = {caret, start};
        if (caret > start && editor.Send(Message::FindTextFull, flags, reinterpret_cast<intptr_t>(&find)) >= 0)
            start = editor.Send(Message::PositionAfter, find.chrgText.cpMin);
        // Original Markdown URL: [alt](url), including balanced URL parentheses.
        auto searchStart = start;
        auto searchEnd = end;
        find.chrg.cpMax = end;
        find.lpstrText = "\\(\\w*:?\\.*/";
        while (searchStart < searchEnd) {
            find.chrg.cpMin = searchStart;
            const auto position = editor.Send(Message::FindTextFull, flags, reinterpret_cast<intptr_t>(&find));
            if (position < 0) break;
            searchStart = position + 1;
            const auto match = editor.Send(Message::BraceMatch, position, find.chrgText.cpMax);
            searchEnd = match < 0 ? end : match;
            if (caret >= searchStart && caret <= searchEnd) {
                start = searchStart;
                end = searchEnd;
                break;
            }
            searchStart = find.chrgText.cpMax;
        }
        return Range(editor, start, end);
    }
    std::string text(static_cast<size_t>(selectionLength) + 1, '\0');
    editor.Send(Message::GetSelText, 0, reinterpret_cast<intptr_t>(text.data()));
    text.resize(text.find_first_of("\r\n\t\0", 0, 4));
    return text;
}
bool FindAnchor(EditorControl& editor, const wxString& target) {
    if (target.size() <= 1 || target[0] != '#') return false;
    std::string escaped;
    for (const auto ch : target.Mid(1).utf8_string()) {
        if (std::string_view(".\\+*?[^]$(){}|-").find(ch) != std::string_view::npos) escaped += '\\';
        escaped += ch;
    }
    for (const char* attribute : {"name", "id"}) {
        const std::string pattern = std::string(attribute) + "\\s*=\\s*['\"]?" + escaped + "['\"]?";
        Scintilla::TextToFindFull find{{0, editor.Send(Message::GetLength)}, pattern.c_str(), 0, {0, 0}};
        constexpr auto flags = Scintilla::FindOption::RegExp | Scintilla::FindOption::Posix;
        if (editor.Send(Message::FindTextFull, static_cast<uintptr_t>(flags), reinterpret_cast<intptr_t>(&find)) >= 0) {
            editor.Send(Message::EnsureVisible, editor.Send(Message::LineFromPosition, find.chrgText.cpMin));
            editor.JumpToPosition(find.chrgText.cpMax);
            editor.Send(Message::SetSel, find.chrgText.cpMin, find.chrgText.cpMax);
            return true;
        }
    }
    return false;
}
wxString TrimTarget(wxString text) {
    constexpr std::string_view trim = " \t\r\n'`\"<>|:*,;";
    const auto isTrim = [trim](wxUniChar ch) {
        return ch.GetValue() < 128 && trim.find(static_cast<char>(ch.GetValue())) != std::string_view::npos;
    };
    while (!text.empty() && isTrim(text.Last()))
        text.RemoveLast();
    while (!text.empty() && isTrim(text[0]))
        text.Remove(0, 1);
    return text;
}
std::optional<wxString> ExistingPath(const wxString& target, const wxString& document) {
    if (target.StartsWith("//")) return {};
    const auto existing = [](const wxString& candidate) -> std::optional<wxString> {
#ifdef __WXMSW__
        // Original EditOpenSelectionCheckFile checks the raw name first.
        // wxFileName::GetFullPath normalizes DOS volumes, so doing that to an
        // unclassified "filename:line:column" or URI can stall on a fake volume.
        if (GetFileAttributesW(candidate.wc_str()) == INVALID_FILE_ATTRIBUTES) return {};
        const auto length = GetFullPathNameW(candidate.wc_str(), 0, nullptr, nullptr);
        if (length != 0) {
            std::wstring absolute(length, L'\0');
            const auto count = GetFullPathNameW(candidate.wc_str(), length, absolute.data(), nullptr);
            if (count != 0 && count < length) {
                absolute.resize(count);
                return wxString(absolute);
            }
        }
        return candidate;
#else
        if (!wxFileExists(candidate) && !wxDirExists(candidate)) return {};
        wxFileName path(candidate);
        path.MakeAbsolute();
        return path.GetFullPath();
#endif
    };
    if (const auto path = existing(target)) return path;
    // Expand only leading native filesystem variables, preserving URL encoding.
#ifdef __WXMSW__
    if (target.StartsWith("%")) {
        const auto length = ExpandEnvironmentStringsW(target.wc_str(), nullptr, 0);
        if (length != 0) {
            std::wstring expanded(length, L'\0');
            const auto count = ExpandEnvironmentStringsW(target.wc_str(), expanded.data(), length);
            if (count != 0 && count <= length) {
                expanded.resize(count - 1);
                if (const auto path = existing(wxString(expanded))) return path;
            }
        }
    }
#else
    if (target.StartsWith("$") || target.StartsWith("~")) {
        wxFileName expanded(target);
        expanded.Normalize(wxPATH_NORM_ENV_VARS | wxPATH_NORM_TILDE | wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
        if (const auto path = existing(expanded.GetFullPath())) return path;
    }
#endif
    if (!document.empty() && wxFileName(target).IsRelative()) {
        const auto candidate = wxFileName(document).GetPathWithSep() + target;
        if (const auto path = existing(candidate)) return path;
    }
    return {};
}
struct Location {
    wxString file;
    long long line = 1;
    long long column = 1;
    bool present = false;
};
Location FileLocation(wxString target) {
    // Pinned EditOpenSelection: trailing dot / ')' and :line[:column] or
    // (line[,column]). Only an existing resulting filename commits the suffix.
    Location result{target};
    // The original ignores ')' only while scanning a possible location; the
    // target itself keeps it, including balanced parentheses inside a URI.
    if (target.EndsWith(")")) target.RemoveLast();
    auto end = target.length();
    auto position = end;
    const auto digit = [](wxUniChar ch) { return ch >= '0' && ch <= '9'; };
    while (position > 0 && digit(target[position - 1]))
        --position;
    if (position == end || position == 0) return result;
    const auto separator = target[position - 1];
    if (separator != ':' && separator != ',' && separator != '(') return result;
    if (!target.Mid(position).ToLongLong(&result.line)) return result;
    auto suffix = position - 1;
    if (separator != '(') {
        end = suffix;
        position = end;
        while (position > 0 && digit(target[position - 1]))
            --position;
        if (position < end && position > 0) {
            result.column = result.line;
            if (!target.Mid(position, end - position).ToLongLong(&result.line)) return result;
            suffix = position - 1;
        }
    }
    result.file = target.Left(suffix);
    result.line = std::max(1LL, result.line);
    result.column = std::max(1LL, result.column);
    result.present = true;
    return result;
}
bool ShowContainingFolder(wxWindow* parent, const wxString& path) {
#ifdef __WXMSW__
    const bool rootDirectory = PathIsRootW(path.wc_str()) != FALSE;
#else
    const bool rootDirectory = wxFileName::DirName(path).GetDirCount() == 0;
#endif
    if (wxDirExists(path) && rootDirectory) {
        wxString error;
        return LaunchDesktopTarget(parent, path, path, error);
    }
    const auto directory = wxFileName(path).GetPath();
    if (!wxDirExists(directory)) return false;
#ifdef __WXMSW__
    // The shell's documented single-item form opens the parent and selects
    // this complete item PIDL. Keep native association opening as the fallback.
    auto* item = ILCreateFromPathW(path.wc_str());
    const HRESULT result = item ? SHOpenFolderAndSelectItems(item, 0, nullptr, 0) : E_FAIL;
#ifdef WXNOTEPAD_DIAGNOSTICS
    // HRESULT is authoritative; tracing preserves the thread's last-error state.
    const auto nativeError = GetLastError();
    if (DiagnosticTrace::Enabled()) {
        constexpr size_t NativePathCharacters = 32768;
        wchar_t nativePath[NativePathCharacters]{};
        const bool decoded = item && SHGetPathFromIDListEx(item, nativePath, NativePathCharacters, GPFIDL_DEFAULT);
        DiagnosticTrace::Write(
            "shell.select-item",
            "\texpected=%s\tnative=%s\tdecoded=%d\tpidl_bytes=%u\tcidl=0\tflags=0\thresult=%08lx\tlast_error=%lu",
            path.utf8_str().data(), wxString(nativePath).utf8_str().data(), decoded, item ? ILGetSize(item) : 0,
            static_cast<unsigned long>(result), nativeError);
    }
    SetLastError(nativeError);
#endif
    CoTaskMemFree(item);
    if (SUCCEEDED(result)) return true;
#elif defined(__WXGTK__)
    GError* failure = nullptr;
    auto* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &failure);
    if (connection) {
        auto* file = g_file_new_for_path(path.utf8_str());
        char* uri = g_file_get_uri(file);
        const char* items[] = {uri, nullptr};
        constexpr int FileManagerRequestTimeoutMilliseconds = 5000;
        auto* result = g_dbus_connection_call_sync(
            connection, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1",
            "ShowItems", g_variant_new("(^ass)", items, ""), nullptr, G_DBUS_CALL_FLAGS_NONE,
            FileManagerRequestTimeoutMilliseconds, nullptr, &failure);
        g_free(uri);
        g_object_unref(file);
        g_object_unref(connection);
        if (result) {
            g_variant_unref(result);
            return true;
        }
    }
    if (failure) g_error_free(failure);
#endif
    wxString error;
    return LaunchDesktopTarget(parent, directory, directory, error);
}
} // namespace

void EditorFrame::AppendOpenTargetCommands(wxMenu* menu) {
    for (const bool folder : {false, true}) {
        int& id = folder ? selectionFolderId_ : openSelectionId_;
        const bool alreadyBound = id != wxID_NONE;
        if (!alreadyBound) id = wxWindow::NewControlId();
        // TRANSLATORS: Original Action on Selection commands; open the target or its containing directory.
        menu->Append(id, folder ? _("Open Containing &Folder") : _("&Open File, Folder, Link, etc."));
        if (alreadyBound) continue;
        Bind(
            wxEVT_MENU,
            [this, folder](wxCommandEvent&) {
                if (!OpenSelection(folder))
                    // TRANSLATORS: Selected file/folder/link launch failure and dialog title; no parameters.
                    wxMessageBox(_("Could not open the selected target."), _("Could not open target"),
                                 wxOK | wxICON_ERROR, this);
            },
            id);
        Bind(
            wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Message::GetLength) != 0); },
            id);
    }
}
bool EditorFrame::OpenSelection(bool containingFolder) {
    auto target = TrimTarget(wxString::FromUTF8(SelectedTarget(*editor_)));
    if (target.empty()) return true;
    if (target.EndsWith(".")) target.RemoveLast();
    if (target.empty()) return true;
    // Original EditOpenSelection ignores punctuation before a scheme/drive name.
    // Preserve absolute native paths that legitimately contain a location colon.
    if (const auto colon = target.find(':');
        colon != wxString::npos && (target.Contains("://") || !wxFileName(target).IsAbsolute())) {
        auto first = colon;
        const auto schemeChar = [](wxUniChar ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '+' ||
                   ch == '-' || ch == '.';
        };
        while (first > 0 && schemeChar(target[first - 1]))
            --first;
        while (first < colon &&
               !((target[first] >= 'a' && target[first] <= 'z') || (target[first] >= 'A' && target[first] <= 'Z')))
            ++first;
        if (first < colon) target = target.Mid(first);
    }
    auto path = ExistingPath(target, document_.Path());
    auto location = FileLocation(target);
    if (!path && location.present)
        path = ExistingPath(location.file, document_.Path());
    else
        location.present = false;
    if (containingFolder && path) return ShowContainingFolder(this, *path);
    if (path && !wxDirExists(*path)) {
        const wxFileName name(*path);
        const auto extension = name.GetExt().Lower();
        const bool supported = location.present || name.GetFullName().StartsWith(".") || extension.empty() ||
                               extension == "cgi" || extension == "fcgi" || DetectLanguage(*path, {}).has_value();
        if (supported) {
            std::vector<wxString> arguments{wxStandardPaths::Get().GetExecutablePath(), "--startup-file", *path};
            if (location.present) {
                arguments.emplace_back("--startup-caret");
                arguments.push_back(wxString::Format("%lld,%lld", location.line, location.column));
            }
            wxString error;
            if (!NativeLaunch::SpawnArguments(arguments, name.GetPath(), error)) {
                return false;
            }
            return true;
        }
    }
    wxString external;
    if (path)
        external = *path;
    else if (target.StartsWith("//"))
        external = "http:" + target;
    else if (target.Contains(':'))
        external = target;
    else if (target.Contains('@'))
        external = "mailto:" + target;
    else if (target.StartsWith("www."))
        external = "http://" + target;
    if (!external.empty()) {
        wxString error;
        return LaunchDesktopTarget(this, external, path ? wxFileName(*path).GetPath() : wxGetCwd(), error);
    }
    FindAnchor(*editor_, target);
    return true;
}
bool EditorFrame::OpenContainingFolder() {
    return !document_.Path().empty() && ShowContainingFolder(this, document_.Path());
}
