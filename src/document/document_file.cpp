#include "document/document_file.h"
#include <array>
#include <optional>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/translation.h>
#include <wx/utils.h>
#include <cstdio>
#ifdef __WXMSW__
#include <filesystem>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
// The original ignores query/restore failures: a successful content save stays
// successful. Preserve every settable time around the existing safe-write path.
#ifdef __WXMSW__
using FileTimes = FILE_BASIC_INFO;
std::optional<FILETIME> ReadAccessTime(const wxString& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.wc_str(), GetFileExInfoStandard, &attributes)) return std::nullopt;
    if (attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        // Path attributes describe a final symbolic link itself; the command's
        // normal CreateFile follows it. Capture the same target's access time.
        std::error_code error;
        const auto target = std::filesystem::canonical(std::filesystem::path(path.ToStdWstring()), error);
        if (error || !GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &attributes)) return std::nullopt;
    }
    return attributes.ftLastAccessTime;
}
std::optional<FileTimes> ReadFileTimes(const wxString& path) {
    const auto file = CreateFileW(path.wc_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    FileTimes times{};
    const bool read = GetFileInformationByHandleEx(file, FileBasicInfo, &times, sizeof(times)) != 0;
    CloseHandle(file);
    return read ? std::optional<FileTimes>(times) : std::nullopt;
}
bool SetFileTimes(const wxString& path, FileTimes times) {
    const auto file = CreateFileW(path.wc_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool set = SetFileInformationByHandle(file, FileBasicInfo, &times, sizeof(times)) != 0;
    CloseHandle(file);
    return set;
}
#else
struct FileTimes {
    timespec access;
    timespec modified;
};
std::optional<FileTimes> ReadFileTimes(const wxString& path) {
    struct stat info{};
    if (::stat(path.utf8_str(), &info) != 0) return std::nullopt;
    return FileTimes{info.st_atim, info.st_mtim};
}
bool SetFileTimes(const wxString& path, const FileTimes& times) {
    const std::array fields{times.access, times.modified};
    return ::utimensat(AT_FDCWD, path.utf8_str(), fields.data(), 0) == 0;
}
#endif

struct PendingSave {
    wxString path;
    wxFile file;
    ~PendingSave() {
        file.Close();
        if (!path.empty()) wxRemoveFile(path);
    }
};

wxString PrepareSave(const wxString& destination, PendingSave& temporary) {
    // Errors are returned to the application instead of opening wx log dialogs.
    wxLogNull quiet;
    if (wxDirExists(destination)) return _("Choose a file as the save destination.");
    if (wxFileExists(destination)) {
        wxFile existing(destination, wxFile::read_write);
        if (!existing.IsOpened()) return _("Could not open the destination for writing.");
    }
    temporary.path = wxFileName::CreateTempFileName(destination + ".wxnotepad-", &temporary.file);
    if (temporary.path.empty())
        return _("Could not create a temporary file beside the destination. Check the folder and access permissions.");
    return {};
}

wxString WriteSafely(const wxString& destination, std::string_view bytes, PendingSave& temporary) {
    wxLogNull quiet;
    if (temporary.file.Write(bytes.data(), bytes.size()) != bytes.size() || !temporary.file.Flush())
        return _("Could not write the complete file. Check free space and access permissions.");
    if (!temporary.file.Close()) return _("Could not close the completed temporary file.");
#ifdef __WXMSW__
    const bool replaced = MoveFileExW(temporary.path.wc_str(), destination.wc_str(),
                                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    const bool replaced = ::rename(temporary.path.utf8_str(), destination.utf8_str()) == 0;
#endif
    if (!replaced) {
        const wxString detail(wxSysErrorMsg());
        return wxString::Format(_("Could not replace the destination: %s"), detail);
    }
    temporary.path.clear();
    return {};
}

} // namespace

namespace DocumentFiles {
struct SaveTransaction::State {
    bool preserveTimes;
    PendingSave temporary;
    std::optional<FileTimes> originalTimes;
};

SaveTransaction::SaveTransaction(bool preserveTimes) : state_(std::make_unique<State>(preserveTimes)) {
}

SaveTransaction::~SaveTransaction() = default;

wxString SaveTransaction::Prepare(const wxString& destination) {
    auto& state = *state_;
    if (!state.temporary.path.empty()) return {};
    const auto error = PrepareSave(destination, state.temporary);
    if (!error.empty()) return error;
    if (state.preserveTimes) {
        // OPEN_ALWAYS in the original captures a new file's initial timestamps.
        // Our new file is the safe-write sibling until replacement commits it.
        state.originalTimes = ReadFileTimes(wxFileExists(destination) ? destination : state.temporary.path);
    }
    return {};
}

wxString SaveTransaction::Commit(const wxString& destination, std::string_view bytes) {
    const auto prepared = Prepare(destination);
    if (!prepared.empty()) return prepared;
    auto& state = *state_;
    const auto error = WriteSafely(destination, bytes, state.temporary);
    if (!error.empty()) return error;
    if (state.originalTimes) SetFileTimes(destination, *state.originalTimes);
    return {};
}

wxString PrepareSaveCopy(const wxString& destination) {
    // Original Save Copy opens/creates the destination before cleanup and
    // legacy-loss confirmation. Cancel keeps a new empty file, while an
    // existing file retains its bytes.
    wxLogNull quiet;
    wxFile file;
    if (wxDirExists(destination)) return _("Choose a file as the save destination.");
    const bool opened =
        wxFileExists(destination) ? file.Open(destination, wxFile::read_write) : file.Create(destination, false);
    if (!opened || !file.Close()) return _("Could not open the destination for writing.");
    return {};
}

namespace {
struct ReadOnlyQuery {
    ReadOnlyState state;
#ifdef __WXMSW__
    DWORD attributes = INVALID_FILE_ATTRIBUTES;
#else
    struct stat info{};
#endif
};

ReadOnlyQuery QueryReadOnlyMetadata(const wxString& path) {
    ReadOnlyQuery result;
    if (path.empty()) {
        // TRANSLATORS: IDM_FILE_READONLY_FILE unavailable for an unnamed document; no parameters.
        result.state.changeError = _("Save the document before changing file permissions.");
        return result;
    }
#ifdef __WXMSW__
    result.attributes = GetFileAttributesW(path.wc_str());
    if (result.attributes == INVALID_FILE_ATTRIBUTES)
        // TRANSLATORS: IDM_FILE_READONLY_FILE metadata error; %s is the native Windows error.
        result.state.changeError = wxString::Format(_("Could not read file attributes: %s"), wxSysErrorMsg());
    else if (result.attributes & FILE_ATTRIBUTE_DIRECTORY)
        // TRANSLATORS: IDM_FILE_READONLY_FILE rejects a directory target; no parameters.
        result.state.changeError = _("This command changes a file, not a directory.");
    else
        result.state.readOnly = result.state.flag = (result.attributes & FILE_ATTRIBUTE_READONLY) != 0;
#else
    if (stat(path.fn_str(), &result.info) != 0)
        // TRANSLATORS: Linux IDM_FILE_READONLY_FILE metadata error; %s is the native filesystem error.
        result.state.changeError = wxString::Format(_("Could not read file permissions: %s"), wxSysErrorMsg());
    else if (!S_ISREG(result.info.st_mode))
        // TRANSLATORS: IDM_FILE_READONLY_FILE rejects a non-regular-file target; no parameters.
        result.state.changeError = _("This command changes a file, not a directory.");
    else {
        result.state.flag = (result.info.st_mode & S_IWUSR) == 0;
        result.state.readOnly = result.info.st_uid == geteuid() ? result.state.flag : !wxFileName::IsFileWritable(path);
        if (result.info.st_uid != geteuid())
            // TRANSLATORS: Linux IDM_FILE_READONLY_FILE ownership restriction; no parameters.
            result.state.changeError = _("Only the file owner can change the owner's write permission.");
    }
#endif
    return result;
}
} // namespace

ReadOnlyState ReadReadOnlyState(const wxString& path) {
    return QueryReadOnlyMetadata(path).state;
}

wxString ToggleReadOnly(const wxString& path) {
    auto query = QueryReadOnlyMetadata(path);
    if (!query.state.changeError.empty()) return query.state.changeError;
#ifdef __WXMSW__
    if (!SetFileAttributesW(path.wc_str(), query.attributes ^ FILE_ATTRIBUTE_READONLY))
        // TRANSLATORS: IDM_FILE_READONLY_FILE update error; %s is the native Windows error.
        return wxString::Format(_("Could not change file attributes: %s"), wxSysErrorMsg());
#else
    if (chmod(path.fn_str(),
              (query.info.st_mode & (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)) ^ S_IWUSR) != 0)
        // TRANSLATORS: Linux IDM_FILE_READONLY_FILE update error; %s is the native filesystem error.
        return wxString::Format(_("Could not change the file owner's write permission: %s"), wxSysErrorMsg());
#endif
    return {};
}

bool UpdateWriteTime(const wxString& path) {
    if (path.empty()) return false;
#ifdef __WXMSW__
    // Opening the update handle can expose a newer access time than the
    // pre-operation path attributes. Preserve that observed value separately.
    const auto originalAccess = ReadAccessTime(path);
    const auto file = CreateFileW(path.wc_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    FILE_BASIC_INFO times{};
    bool updated = GetFileInformationByHandleEx(file, FileBasicInfo, &times, sizeof(times)) != 0;
    if (updated) {
        if (originalAccess) {
            times.LastAccessTime.LowPart = originalAccess->dwLowDateTime;
            times.LastAccessTime.HighPart = static_cast<LONG>(originalAccess->dwHighDateTime);
        }
        GetSystemTimePreciseAsFileTime(reinterpret_cast<FILETIME*>(&times.LastWriteTime));
        updated = SetFileInformationByHandle(file, FileBasicInfo, &times, sizeof(times)) != 0;
    }
    CloseHandle(file);
#else
    const int file = ::open(path.utf8_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (file < 0) return false;
    // Preserve access time; ctime follows the filesystem's metadata-update rules.
    const std::array<timespec, 2> times{{{0, UTIME_OMIT}, {0, UTIME_NOW}}};
    const bool updated = ::futimens(file, times.data()) == 0;
    ::close(file);
#endif
    return updated;
}

} // namespace DocumentFiles
