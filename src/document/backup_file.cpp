#include "document/backup_file.h"
#include "editor/platform/native_encoding.h"
#include <wx/datetime.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <string>
#include <utility>
#ifdef __WXMSW__
#include <shlobj.h>
#include <windows.h>
#endif

namespace {
wxString BackupPath(const wxString& source, bool automatic) {
    // Notepad4 extracts from the first dot of the full path, then removes
    // the automatic timestamp suffix at the last space in the constructed name.
    const auto dot = source.find('.');
    const auto extension = dot == wxString::npos ? wxString(".bak") : source.Mid(dot);
    const auto time = wxDateTime::UNow();
    auto backup = source + time.Format(" %Y%m%d %H%M%S");
    if (automatic)
        return backup + wxString::Format(" %03u%lu", static_cast<unsigned>(time.GetMillisecond()), wxGetProcessId()) +
               extension;
    if (const auto space = extension.rfind(' '); space != wxString::npos)
        backup += wxString::Format(" %03u%lu", static_cast<unsigned>(time.GetMillisecond()), wxGetProcessId()) +
                  extension.Left(space);
    return backup + extension;
}

wxString DefaultBackupDirectory() {
    wxFileName directory;
#ifdef __WXMSW__
    PWSTR local = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local) != S_OK)
        return wxFileName(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(), "AutoSave").GetFullPath();
    directory = wxFileName::DirName(local);
    CoTaskMemFree(local);
#else
    wxString root;
    if (wxGetEnv("XDG_DATA_HOME", &root) && wxFileName(root).IsAbsolute()) {
        directory = wxFileName::DirName(root);
    } else {
        directory = wxFileName::DirName(wxGetHomeDir());
        directory.AppendDir(".local");
        directory.AppendDir("share");
    }
#endif
    directory.AppendDir("Notepad4");
    directory.AppendDir("AutoSave");
    return directory.GetPath();
}

std::string_view BackupMetadataEnding(Scintilla::EndOfLine mode) {
    using enum Scintilla::EndOfLine;
    if (mode == Lf) return "\n";
    if (mode == Cr) return "\r";
    return "\r\n";
}

enum class BackupCreation { OpenFailed, WriteFailed, Complete };

BackupCreation CreateBackup(const wxString& path, std::string_view bytes) {
#ifdef __WXMSW__
    const auto file = CreateFileW(path.wc_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return BackupCreation::OpenFailed;
    DWORD written = 0;
    const bool complete = bytes.size() <= MAXDWORD &&
                          WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                          written == bytes.size();
    CloseHandle(file);
#else
    wxFile file;
    if (!file.Create(path, false)) return BackupCreation::OpenFailed;
    const bool written = file.Write(bytes.data(), bytes.size()) == bytes.size();
    const bool complete = file.Close() && written;
#endif
    // Only a failure to create the file permits the caller to try a fallback.
    if (!complete) wxRemoveFile(path);
    return complete ? BackupCreation::Complete : BackupCreation::WriteFailed;
}

} // namespace

namespace DocumentBackups {
wxString Directory() {
    return DefaultBackupDirectory();
}

wxString Write(const wxString& source, std::string_view bytes, bool automatic, bool nativeDefault,
               Scintilla::EndOfLine lineEnding) {
    const wxLogNull quiet;
    const bool untitled = source.empty();
    const auto sibling = BackupPath(untitled ? wxString("Untitled") : source, automatic);
    if (!untitled) {
        const auto result = CreateBackup(sibling, bytes);
        if (result != BackupCreation::OpenFailed) return result == BackupCreation::Complete ? sibling : wxString{};
    }

    const auto directory = Directory();
    wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const auto backup = wxFileName(directory, wxFileName(sibling).GetFullName()).GetFullPath();
    if (untitled) return CreateBackup(backup, bytes) == BackupCreation::Complete ? backup : wxString{};
    auto metadata = ("AutoSave for " + source).utf8_string();
    auto output = nativeDefault ? NativeDefaultEncodingBytes(metadata) : std::move(metadata);
    output += BackupMetadataEnding(lineEnding);
    output += bytes;
    return CreateBackup(backup, output) == BackupCreation::Complete ? backup : wxString{};
}

} // namespace DocumentBackups
