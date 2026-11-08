#include "settings/recent_files.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <algorithm>
#ifdef __WXMSW__
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#endif

namespace {
wxString ApplicationDirectory() {
    return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
}
wxString DocumentsDirectory() {
#ifdef __WXMSW__
    PWSTR directory = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &directory))) return {};
    const wxString result(directory);
    CoTaskMemFree(directory);
    return result;
#else
    return wxStandardPaths::Get().GetDocumentsDir();
#endif
}
} // namespace

bool RecentFiles::Equal(const wxString& left, const wxString& right) {
#ifdef __WXMSW__
    return CompareStringOrdinal(left.wc_str(), -1, right.wc_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

void RecentFiles::Add(std::vector<wxString>& history, const wxString& path, int capacity) {
    if (path.empty()) return;
    const auto existing =
        std::find_if(history.begin(), history.end(), [&](const auto& item) { return Equal(item, path); });
    // MRUList::Add retains an existing entry's spelling when moving it to front.
    if (existing != history.end())
        std::rotate(history.begin(), existing, existing + 1);
    else
        history.insert(history.begin(), path);
    if (history.size() > static_cast<size_t>(capacity)) history.resize(capacity);
}

wxString RecentFiles::ExpandPath(wxString path) {
    constexpr auto documentsToken = "%CSIDL:MYDOCUMENTS%";
    if (path.StartsWith(documentsToken)) {
        path = path.Mid(wxString(documentsToken).length());
        if (path.StartsWith("\\") || path.StartsWith("/")) path = path.Mid(1);
        path = wxFileName(DocumentsDirectory(), path).GetFullPath();
    }
#ifdef __WXMSW__
    const DWORD size = ExpandEnvironmentStringsW(path.wc_str(), nullptr, 0);
    if (size) {
        std::wstring expanded(size, L'\0');
        if (ExpandEnvironmentStringsW(path.wc_str(), expanded.data(), size)) path = expanded.c_str();
    }
#endif
    wxFileName filename(path);
    filename.Normalize(wxPATH_NORM_ENV_VARS | wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_TILDE,
                       ApplicationDirectory());
    return filename.GetFullPath();
}

wxString RecentFiles::PortablePath(const wxString& path, bool relative, bool portableDocuments) {
    if (!relative) return path;
    const wxString application = ApplicationDirectory();
    const wxString documents = DocumentsDirectory();
#ifdef __WXMSW__
    // Reuse PathRelativeToApp's Documents, Windows-directory and environment rules.
    wchar_t converted[MAX_PATH]{};
    wxString result = path;
    bool substituted = false;
    if (portableDocuments && !documents.empty() && !PathIsPrefixW(documents.wc_str(), application.wc_str()) &&
        PathIsPrefixW(documents.wc_str(), path.wc_str()) &&
        PathRelativePathToW(converted, documents.wc_str(), FILE_ATTRIBUTE_DIRECTORY, path.wc_str(), 0)) {
        wchar_t combined[MAX_PATH]{};
        if (PathCombineW(combined, L"%CSIDL:MYDOCUMENTS%", converted)) {
            result = combined;
            substituted = true;
        }
    }
    wchar_t windows[MAX_PATH]{};
    if (!substituted && GetWindowsDirectoryW(windows, MAX_PATH) &&
        !PathCommonPrefixW(application.wc_str(), windows, nullptr) &&
        PathRelativePathToW(converted, application.wc_str(), FILE_ATTRIBUTE_DIRECTORY, path.wc_str(), 0))
        result = converted;
    if (PathUnExpandEnvStringsW(result.wc_str(), converted, MAX_PATH)) result = converted;
    return result;
#else
    wxFileName filename(path);
    const wxString prefix = documents + wxFileName::GetPathSeparator();
    if (portableDocuments && !documents.empty() && !application.StartsWith(prefix) && path.StartsWith(prefix))
        return "%CSIDL:MYDOCUMENTS%/" + path.Mid(prefix.length());
    if (filename.MakeRelativeTo(application)) return filename.GetFullPath();
    return path;
#endif
}
