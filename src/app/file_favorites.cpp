#include "app/file_favorites.h"
#include "settings/environment_expansion.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/translation.h>
#include <cerrno>
#include <filesystem>
#ifdef __WXMSW__
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#else
#include <unistd.h>
#endif

namespace {
#ifdef __WXMSW__
class FavoriteShellLink final {
public:
    FavoriteShellLink() { CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)); }
    ~FavoriteShellLink() {
        if (link) link->Release();
    }
    IShellLinkW* link = nullptr;
};
#endif
} // namespace

wxString FileFavorites::Directory(const wxString& configured) {
    if (!configured.empty()) return configured;
#ifdef __WXMSW__
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &path))) {
        const wxString directory(path);
        CoTaskMemFree(path);
        return directory;
    }
#endif
    return wxStandardPaths::Get().GetDocumentsDir();
}

wxString FileFavorites::DefaultName(const wxString& target) {
#ifdef __WXMSW__
    SHFILEINFOW info{};
    if (SHGetFileInfoW(target.wc_str(), 0, &info, sizeof(info), SHGFI_DISPLAYNAME)) return info.szDisplayName;
#endif
    return wxFileName(target).GetFullName();
}

wxString FileFavorites::DisplayName(const wxString& entry) {
    auto name = wxFileName(entry).GetFullName();
#ifdef __WXMSW__
    if (name.Lower().EndsWith(".lnk")) name.RemoveLast(4);
#endif
    return name;
}

wxString FileFavorites::Add(const wxString& directory, const wxString& name, const wxString& target) {
    if (name.empty() || name == "." || name == ".." || name.Contains('/') || name.Contains('\\') || name.Contains('\0'))
        return _("Enter a valid name for the favorite.");
#ifdef __WXMSW__
    if (name.find_first_of("<>:\"|?*") != wxString::npos || name.EndsWith(".") || name.EndsWith(" "))
        return _("Enter a valid name for the favorite.");
    const auto destination = wxFileName(directory, name + ".lnk").GetFullPath();
    // Reserve the destination atomically: IPersistFile::Save normally overwrites.
    const HANDLE reserved =
        CreateFileW(destination.wc_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (reserved == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS)
            return _("A favorite with this name already exists.");
        return _("Could not create the favorite. Check the name and directory permissions.");
    }
    CloseHandle(reserved);
    FavoriteShellLink shell;
    IPersistFile* file = nullptr;
    bool saved = false;
    if (shell.link && SUCCEEDED(shell.link->SetPath(target.wc_str())) &&
        SUCCEEDED(shell.link->QueryInterface(IID_PPV_ARGS(&file)))) {
        saved = SUCCEEDED(file->Save(destination.wc_str(), TRUE));
        file->Release();
    }
    if (saved) return {};
    DeleteFileW(destination.wc_str());
#else
    const auto destination = wxFileName(directory, name).GetFullPath();
    if (symlink(target.utf8_str(), destination.utf8_str()) == 0) return {};
    if (errno == EEXIST) return _("A favorite with this name already exists.");
#endif
    return _("Could not create the favorite. Check the name and directory permissions.");
}

std::optional<wxString> FileFavorites::Resolve(const wxString& entry) {
#ifdef __WXMSW__
    if (wxFileName(entry).GetExt().CmpNoCase("lnk") == 0) {
        FavoriteShellLink shell;
        IPersistFile* file = nullptr;
        if (!shell.link || FAILED(shell.link->QueryInterface(IID_PPV_ARGS(&file)))) return {};
        const HRESULT loaded = file->Load(entry.wc_str(), STGM_READ);
        file->Release();
        if (FAILED(loaded)) return {};
        constexpr size_t WindowsPathCapacity = 32768;
        wchar_t target[WindowsPathCapacity]{};
        if (FAILED(shell.link->GetPath(target, static_cast<int>(std::size(target)), nullptr, 0)) || !*target) return {};
        wxFileName targetPath(ExpandWindowsEnvironment(target));
        targetPath.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE, wxFileName(entry).GetPath());
        const wxString resolved = targetPath.GetFullPath();
        return resolved;
    }
    return entry;
#else
    // Linux favorites are symbolic links, including relative links and chains.
    // Never interpret an imported Windows shortcut's binary bytes as document text.
    if (wxFileName(entry).GetExt().CmpNoCase("lnk") == 0) return {};
    std::error_code error;
    auto path = std::filesystem::path(entry.utf8_str().data());
    constexpr size_t MaximumSymbolicLinks = 40;
    for (size_t depth = 0; depth <= MaximumSymbolicLinks; ++depth) {
        const auto status = std::filesystem::symlink_status(path, error);
        if (std::filesystem::is_symlink(status)) {
            if (depth == MaximumSymbolicLinks) return {};
            const auto target = std::filesystem::read_symlink(path, error);
            if (error) return {};
            path = (target.is_absolute() ? target : path.parent_path() / target).lexically_normal();
        } else {
            if (error && error != std::errc::no_such_file_or_directory) return {};
            // Preserve the actual missing target for the original Create prompt.
            error.clear();
            path = std::filesystem::weakly_canonical(path, error);
            if (!error) return wxString::FromUTF8(path.string());
            return {};
        }
    }
#endif
    return {};
}
