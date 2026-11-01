#include "settings/settings_store.h"
#include <wx/ffile.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>
#include <string>
#ifdef __WXMSW__
#include <shlobj.h>
#include <filesystem>
#endif

namespace {
wxString ConfigurationPath() {
    wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
#ifdef __WXMSW__
    // Notepad4 resolves an executable symbolic link before FindIniFile.
    std::error_code error;
    const auto realPath = std::filesystem::canonical(executable.GetFullPath().ToStdWstring(), error);
    if (!error) executable.Assign(realPath.wstring());
#endif
    wxString directory = executable.GetPath();
#ifdef __WXMSW__
    // Preserve FindIniFile's installation detection and LocalAppData fallback.
    const wxString module = executable.GetFullPath();
    if (module.StartsWith("C:\\Program Files") || module.Contains("WinGet") || module.Contains("hocolatey")) {
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local))) {
            directory = wxFileName(local, "Notepad4").GetFullPath();
            CoTaskMemFree(local);
        }
    }
#else
    // Native Linux installation directories map to the XDG user config directory.
    const wxString module = executable.GetFullPath();
    if (module.StartsWith("/usr/") || module.StartsWith("/opt/") || module.StartsWith("/app/") ||
        module.StartsWith("/snap/") || module.StartsWith("/bin/")) {
        wxString config;
        if (!wxGetEnv("XDG_CONFIG_HOME", &config) || !wxFileName(config).IsAbsolute())
            config = wxFileName(wxGetHomeDir(), ".config").GetFullPath();
        directory = wxFileName(config, "Notepad4").GetFullPath();
    }
#endif
    wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString path = wxFileName(directory, "Notepad4.ini").GetFullPath();
    if (!wxFileExists(path)) {
        const wxString source = wxFileName(executable.GetPath(), "Notepad4.ini-default").GetFullPath();
        if (wxFileExists(source)) wxCopyFile(source, path, false);
    }
    if (!wxFileExists(path) || wxFileName(path).GetSize() < 2) {
        wxFFile output(path, "wb");
        if (output.IsOpened()) output.Write(wxString(wxUniChar(0xFEFF)) + "[Notepad4]\r\n", wxMBConvUTF16LE());
    }
    return path;
}
} // namespace

SettingsStore::SettingsStore() : path_(ConfigurationPath()) {
    LoadLines();
    values.saveSettings = ReadBool("SaveSettings", true);
    values.wordWrap = ReadBool("WordWrap", true);
    values.showLineNumbers = ReadBool("ShowLineNumbers", true);
    values.viewWhiteSpace = ReadBool("ViewWhiteSpace", false);
}

void SettingsStore::LoadLines() {
    lines_.clear();
    wxString text;
    if (wxFileExists(path_)) {
        wxFFile input(path_, "rb");
        if (input.IsOpened()) {
            std::string bytes(static_cast<size_t>(input.Length()), '\0');
            if (input.Read(bytes.data(), bytes.size()) == bytes.size()) {
#ifdef __WXMSW__
                if (wxConvAuto::DetectBOM(bytes.data(), bytes.size()) == wxBOM_None)
                    text = wxString(bytes.data(), wxConvLocal, bytes.size());
                else
#endif
                    text = wxString(bytes.data(), wxConvAuto(wxFONTENCODING_SYSTEM), bytes.size());
            }
        }
    }
    wxStringTokenizer lines(text, "\n", wxTOKEN_RET_EMPTY_ALL);
    while (lines.HasMoreTokens()) {
        wxString line = lines.GetNextToken();
        if (line.EndsWith("\r")) line.RemoveLast();
        lines_.push_back(line);
    }
    if (!lines_.empty() && lines_.back().empty()) lines_.pop_back();
}

bool SettingsStore::ReadBool(const wxString& key, bool fallback) const {
    bool settings = false;
    for (const auto& raw : lines_) {
        wxString line = raw;
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            settings = line.CmpNoCase("[Settings]") == 0;
        } else if (settings && line.Contains('=') && line.BeforeFirst('=').Trim().Trim(false) == key) {
            const wxString value = line.AfterFirst('=').Trim().Trim(false);
            // Original IniSectionParser::GetBoolImpl accepts a leading 0 or 1.
            if (value.StartsWith("0")) return false;
            if (value.StartsWith("1")) return true;
            return fallback;
        }
    }
    return fallback;
}

void SettingsStore::WriteBool(const wxString& key, bool value, bool fallback) {
    bool settings = false;
    bool foundSection = false;
    size_t insertion = lines_.size();
    for (size_t index = 0; index < lines_.size();) {
        wxString line = lines_[index];
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            settings = line.CmpNoCase("[Settings]") == 0;
            if (settings && !foundSection) {
                insertion = index + 1;
                foundSection = true;
            }
        } else if (settings && line.Contains('=') && line.BeforeFirst('=').Trim().Trim(false) == key) {
            lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        ++index;
    }
    if (value == fallback) return;
    if (!foundSection) {
        lines_.push_back("[Settings]");
        insertion = lines_.size();
    }
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insertion), key + (value ? "=1" : "=0"));
}

bool SettingsStore::Save(bool explicitSave) {
    if (wxFileExists(path_) && !wxIsWritable(path_)) return false;
    // Merge the implemented fields into the current file, retaining other fields
    // and sections, including settings added by subsequent parity slices.
    LoadLines();
    WriteBool("SaveSettings", values.saveSettings, true);
    if (values.saveSettings || explicitSave) {
        WriteBool("WordWrap", values.wordWrap, true);
        WriteBool("ShowLineNumbers", values.showLineNumbers, true);
        WriteBool("ViewWhiteSpace", values.viewWhiteSpace, false);
    }
    wxString text(wxUniChar(0xFEFF));
    for (const auto& line : lines_)
        text += line + "\r\n";
    wxTempFile output(path_);
    return output.IsOpened() && output.Write(text, wxMBConvUTF16LE()) && output.Commit();
}
