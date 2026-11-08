#include "settings/settings_store.h"
#include "search/search_limits.h"
#include "settings/recent_files.h"
#include <ScintillaTypes.h>
#include <wx/ffile.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <limits>
#ifdef __WXMSW__
#include <shlobj.h>
#include <filesystem>
#endif

namespace {
constexpr const char* SchemeSections[] = {"Text File", "C/C++ Source", "Python Script", "JSON Document"};
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
wxString ParseDateTimeFormat(wxString value) {
    value.Trim().Trim(false);
    if (value.length() >= 2 &&
        ((value.StartsWith("\"") && value.EndsWith("\"")) || (value.StartsWith("'") && value.EndsWith("'"))))
        value = value.Mid(1, value.length() - 2);
    // IniGetString's original WCHAR[256] reserves one UTF-16 unit for NUL.
    wxString truncated;
    size_t units = 0;
    for (const wxUniChar character : value) {
        if (units == 255) break;
        const size_t count = character.GetValue() >= 0x10000 ? 2 : 1;
        if (units + count > 255) {
            truncated += wxUniChar(0xfffd);
            break;
        }
        truncated += character;
        units += count;
    }
    return truncated;
}
} // namespace

SettingsStore::SettingsStore() : path_(ConfigurationPath()) {
    const OriginalSettings defaults;
    LoadLines();
    values.saveSettings = ReadBool("SaveSettings", defaults.saveSettings);
    const int recent = ReadInt("SaveRecentFiles", RecentFiles::MinimumCapacity << 1);
    values.saveRecentFiles = (recent & 1) != 0;
    values.maxRecentFiles = std::max(recent >> 1, RecentFiles::MinimumCapacity);
    recentFileCapacity_ = values.maxRecentFiles;
    values.relativeFileMRU = ReadBool("RelativeFileMRU", true, "Settings2");
    values.portableMyDocs = ReadBool("PortableMyDocs", values.relativeFileMRU, "Settings2");
    if (values.saveRecentFiles) values.recentFiles = ReadRecentFiles();
    values.wordWrap = ReadBool("WordWrap", defaults.wordWrap);
    values.autoShrinkScrollWidth = ReadBool("AutoShrinkScrollWidth", defaults.autoShrinkScrollWidth, "wxnotepad");
    values.showLineNumbers = ReadBool("ShowLineNumbers", defaults.showLineNumbers);
    values.showBookmarkMargin = ReadBool("ShowBookmarkMargin", defaults.showBookmarkMargin);
    values.changeHistoryMarker = ReadInt("ChangeHistoryMarker", defaults.changeHistoryMarker);
    values.matchBraces = ReadBool("MatchBraces", defaults.matchBraces);
    values.showCodeFolding = ReadBool("ShowCodeFolding", defaults.showCodeFolding);
    values.highlightCurrentBlock = ReadBool("HighlightCurrentBlock", defaults.highlightCurrentBlock);
    values.viewWhiteSpace = ReadBool("ViewWhiteSpace", defaults.viewWhiteSpace);
    values.showIndentGuides = ReadBool("ShowIndentGuides", defaults.showIndentGuides);
    values.simpleIndentGuides = ReadBool("SimpleIndentGuides", defaults.simpleIndentGuides, "Settings2");
    values.viewEOLs = ReadBool("ViewEOLs", defaults.viewEOLs);
    values.showWordWrapSymbols = ReadBool("ShowWordWrapSymbols", defaults.showWordWrapSymbols);
    const int wrapSymbols = std::clamp(ReadInt("WordWrapSymbols", defaults.wordWrapSymbols), 0, 32);
    values.wordWrapSymbols = std::min(wrapSymbols % 10, 2) + (wrapSymbols / 10) * 10;
    values.autoSelect = ReadBool("AutoSelect", defaults.autoSelect, "Styles");
    values.noCgiGuess = ReadBool("NoCGIGuess", defaults.noCgiGuess, "Settings2");
    values.fixLineEndings = ReadBool("FixLineEndings", defaults.fixLineEndings);
    values.fixTrailingBlanks = ReadBool("FixTrailingBlanks", defaults.fixTrailingBlanks);
    values.fileWatching.mode = static_cast<FileWatchingMode>(
        std::clamp(ReadInt("FileWatchingMode", static_cast<int>(defaults.fileWatching.mode)), 0, 2));
    values.fileWatching.checkInterval = ReadInt("FileCheckInterval", defaults.fileWatching.checkInterval, "Settings2");
    values.fileWatching.autoReloadDelay =
        ReadInt("AutoReloadTimeout", defaults.fileWatching.autoReloadDelay, "Settings2");
    values.fileWatching.flags = ReadInt("FileWatchingOption", defaults.fileWatching.flags);
    values.fileWatching.resetOnNewFile = ReadBool("ResetFileWatching", defaults.fileWatching.resetOnNewFile);
    values.encoding.defaultEncoding =
        NormalizeEncodingSetting(ReadInt("DefaultEncoding", defaults.encoding.defaultEncoding));
    values.encoding.skipUnicodeDetection = ReadBool("SkipUnicodeDetection", defaults.encoding.skipUnicodeDetection);
    values.encoding.loadANSIasUTF8 = ReadBool("LoadANSIasUTF8", defaults.encoding.loadANSIasUTF8);
    values.encoding.loadASCIIasUTF8 = ReadBool("LoadASCIIasUTF8", defaults.encoding.loadASCIIasUTF8);
    values.encoding.loadNFOasOEM = ReadBool("LoadNFOasOEM", defaults.encoding.loadNFOasOEM);
    values.encoding.noEncodingTags = ReadBool("NoEncodingTags", defaults.encoding.noEncodingTags);
    values.encoding.noFileVariables = ReadBool("NoFileVariables", defaults.encoding.noFileVariables, "Settings2");
    values.zoomLevel =
        std::clamp(ReadInt("ZoomLevel", defaults.zoomLevel), Scintilla::MinZoomLevel, Scintilla::MaxZoomLevel);
    int caretStyle = ReadInt("CaretStyle", defaults.caret.lineWidth);
    values.caret.blockAfterSelection = caretStyle >= 100;
    caretStyle %= 100;
    values.caret.overstrikeBlock = caretStyle >= 10;
    values.caret.lineWidth = std::clamp(caretStyle % 10, 0, 3);
    values.caret.blinkPeriod = ReadInt("CaretBlinkPeriod", defaults.caret.blinkPeriod);
    const int currentLine = ReadInt("HighlightCurrentLine", defaults.highlightCurrentLine);
    values.highlightCurrentLine = std::clamp(currentLine % 10, 0, 2) + (currentLine >= 10 ? 10 : 0);
    values.longLinesLimit =
        std::clamp(ReadInt("LongLinesLimit", defaults.longLinesLimit), 0, OriginalSettingLimits::MaxLongLineColumn);
    values.markLongLines = ReadBool("MarkLongLines", defaults.markLongLines);
    values.longLineMode = std::clamp(ReadInt("LongLineMode", defaults.longLineMode), 1, 2);
    values.selectOption = ReadInt("SelectOption", defaults.selectOption);
    values.autoInsertMask = ReadInt("AutoInsertMask", defaults.autoInsertMask);
    values.markOccurrences = ReadInt("MarkOccurrences", defaults.markOccurrences);
    values.saveFindReplace = ReadBool("SaveFindReplace", defaults.saveFindReplace);
    values.findReplaceOption = ReadInt("FindReplaceOption", defaults.findReplaceOption);
    if (values.saveFindReplace) {
        values.findReplaceFlag = ReadInt("FindReplaceFlag", defaults.findReplaceFlag);
        values.recentFind = ReadHistory("Recent Find", SearchLimits::HistoryEntries);
        values.recentReplace = ReadHistory("Recent Replace", SearchLimits::HistoryEntries);
    }
    values.lineSelection = ReadInt("LineSelection", defaults.lineSelection);
    values.defaultEOLMode = std::clamp(ReadInt("DefaultEOLMode", defaults.defaultEOLMode), 0, 2);
    values.warnLineEndings = ReadBool("WarnLineEndings", defaults.warnLineEndings);
    values.globalTabs.tabWidth =
        std::clamp(ReadInt("TabWidth", defaults.globalTabs.tabWidth), 1, OriginalSettingLimits::MaxTabWidth);
    values.globalTabs.indentWidth =
        std::clamp(ReadInt("IndentWidth", defaults.globalTabs.indentWidth), 0, OriginalSettingLimits::MaxTabWidth);
    values.globalTabs.tabsAsSpaces = ReadBool("TabsAsSpaces", defaults.globalTabs.tabsAsSpaces);
    values.tabIndents = ReadBool("TabIndents", defaults.tabIndents);
    values.autoIndent = ReadBool("AutoIndent", defaults.autoIndent);
    values.detectIndentation = ReadBool("DetectIndentation", defaults.detectIndentation);
    values.backspaceUnindents = static_cast<uint8_t>(ReadInt("BackspaceUnindents", defaults.backspaceUnindents));
    values.dateTimeShort = ReadDateTimeFormat("DateTimeShort");
    values.dateTimeLong = ReadDateTimeFormat("DateTimeLong");
    for (size_t index = 0; index < values.schemeTabs.size(); ++index) {
        auto& tabs = values.schemeTabs[index];
        tabs.tabWidth = std::clamp(ReadInt("TabWidth", tabs.tabWidth, SchemeSections[index]), 1,
                                   OriginalSettingLimits::MaxTabWidth);
        tabs.indentWidth = std::clamp(ReadInt("IndentWidth", tabs.indentWidth, SchemeSections[index]), 0,
                                      OriginalSettingLimits::MaxTabWidth);
        tabs.tabsAsSpaces = ReadInt("TabsAsSpaces", tabs.tabsAsSpaces, SchemeSections[index]) != 0;
        tabs.useGlobal = ReadInt("UseGlobalTabSettings", tabs.useGlobal, SchemeSections[index]) != 0;
    }
}

bool SettingsStore::LoadLines() {
    lines_.clear();
    wxString text;
    if (wxFileExists(path_)) {
        wxFFile input(path_, "rb");
        if (!input.IsOpened()) return false;
        const auto length = input.Length();
        if (length < 0) return false;
        std::string bytes(static_cast<size_t>(length), '\0');
        if (input.Read(bytes.data(), bytes.size()) != bytes.size() || input.Error() || !input.Close()) return false;
#ifdef __WXMSW__
        if (wxConvAuto::DetectBOM(bytes.data(), bytes.size()) == wxBOM_None)
            text = wxString(bytes.data(), wxConvLocal, bytes.size());
        else
#endif
            text = wxString(bytes.data(), wxConvAuto(wxFONTENCODING_SYSTEM), bytes.size());
    }
    wxStringTokenizer lines(text, "\n", wxTOKEN_RET_EMPTY_ALL);
    while (lines.HasMoreTokens()) {
        wxString line = lines.GetNextToken();
        if (line.EndsWith("\r")) line.RemoveLast();
        lines_.push_back(line);
    }
    if (!lines_.empty() && lines_.back().empty()) lines_.pop_back();
    return true;
}

bool SettingsStore::ReadBool(const wxString& key, bool fallback, const wxString& sectionName) const {
    bool settings = false;
    for (const auto& raw : lines_) {
        wxString line = raw;
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            settings = line.CmpNoCase("[" + sectionName + "]") == 0;
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

wxString SettingsStore::ReadDateTimeFormat(const wxString& key) const {
    bool section = false;
    for (const auto& raw : lines_) {
        wxString line = raw;
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            section = line.CmpNoCase("[Settings2]") == 0;
        } else if (section && line.Contains('=') && line.BeforeFirst('=').Trim().Trim(false).CmpNoCase(key) == 0)
            return ParseDateTimeFormat(line.AfterFirst('='));
    }
    return {};
}

int SettingsStore::ReadInt(const wxString& key, int fallback, const wxString& sectionName) const {
    bool settings = false;
    for (const auto& raw : lines_) {
        wxString line = raw;
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            settings = line.CmpNoCase("[" + sectionName + "]") == 0;
        } else if (settings && line.Contains('=') && line.BeforeFirst('=').Trim().Trim(false) == key) {
            // Original CRTStrToInt accepts a decimal prefix (including a sign).
            const auto value = line.AfterFirst('=').ToStdString(wxConvUTF8);
            char* end = nullptr;
            const long number = std::strtol(value.c_str(), &end, 10);
            if (end == value.c_str()) return fallback;
            return static_cast<int>(std::clamp(number, static_cast<long>(std::numeric_limits<int>::min()),
                                               static_cast<long>(std::numeric_limits<int>::max())));
        }
    }
    return fallback;
}

void SettingsStore::WriteBool(const wxString& key, bool value, bool fallback, const wxString& sectionName) {
    WriteInt(key, value, fallback, sectionName);
}

std::vector<wxString> SettingsStore::ReadHistory(const wxString& sectionName, size_t capacity) const {
    std::vector<wxString> history;
    bool section = false;
    for (const auto& raw : lines_) {
        wxString line = raw;
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            section = line.CmpNoCase("[" + sectionName + "]") == 0;
        } else if (section && !line.StartsWith(";") && !line.StartsWith("#") && line.Contains('=')) {
            wxString value = line.AfterFirst('=').Trim().Trim(false);
            if (value.StartsWith("\"") && value.EndsWith("\"") && value.length() >= 2)
                value = value.Mid(1, value.length() - 2);
            if (!value.empty()) history.push_back(value);
            if (history.size() == capacity) break;
        }
    }
    return history;
}

std::vector<wxString> SettingsStore::ReadRecentFiles() const {
    auto history = ReadHistory("Recent Files", recentFileCapacity_);
    for (auto& path : history)
        path = RecentFiles::ExpandPath(path);
    return history;
}

void SettingsStore::WriteRecentFiles(const std::vector<wxString>& history) {
    bool section = false;
    for (size_t index = 0; index < lines_.size();) {
        wxString line = lines_[index];
        line.Trim().Trim(false);
        if (line.StartsWith("[")) section = line.CmpNoCase("[Recent Files]") == 0;
        if (section)
            lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(index));
        else
            ++index;
    }
    if (history.empty()) return;
    lines_.push_back("[Recent Files]");
    for (size_t index = 0; index < history.size(); ++index)
        lines_.push_back(wxString::Format("%02zu=", index + 1) +
                         RecentFiles::PortablePath(history[index], values.relativeFileMRU, values.portableMyDocs));
}

void SettingsStore::AddRecentFile(const wxString& path) {
    RecentFiles::Add(values.recentFiles, path, recentFileCapacity_);
}

void SettingsStore::ClearRecentFiles(const wxString& currentPath) {
    values.recentFiles.clear();
    if (values.saveRecentFiles && LoadLines()) {
        WriteRecentFiles({});
        SaveLines();
    }
    AddRecentFile(currentPath);
}

void SettingsStore::RemoveRecentFile(size_t index) {
    if (index >= values.recentFiles.size()) return;
    const wxString removed = values.recentFiles[index];
    if (LoadLines()) {
        auto stored = ReadRecentFiles();
        std::erase_if(stored, [&](const auto& path) { return RecentFiles::Equal(path, removed); });
        WriteRecentFiles(stored);
        SaveLines();
    }
    values.recentFiles.erase(values.recentFiles.begin() + static_cast<std::ptrdiff_t>(index));
}

void SettingsStore::WriteHistory(const wxString& sectionName, const std::vector<wxString>& history) {
    if (history.empty()) return;
    auto merged = ReadHistory(sectionName, SearchLimits::HistoryEntries);
    for (auto item = history.rbegin(); item != history.rend(); ++item) {
        std::erase(merged, *item);
        merged.insert(merged.begin(), *item);
    }
    if (merged.size() > SearchLimits::HistoryEntries) merged.resize(SearchLimits::HistoryEntries);
    bool section = false;
    for (size_t index = 0; index < lines_.size();) {
        wxString line = lines_[index];
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            section = line.CmpNoCase("[" + sectionName + "]") == 0;
        }
        if (section)
            lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(index));
        else
            ++index;
    }
    lines_.push_back("[" + sectionName + "]");
    for (size_t index = 0; index < merged.size(); ++index)
        lines_.push_back(wxString::Format("%02zu=\"", index + 1) + merged[index] + "\"");
}

void SettingsStore::WriteInt(const wxString& key, int value, int fallback, const wxString& sectionName) {
    bool settings = false;
    bool foundSection = false;
    size_t insertion = lines_.size();
    for (size_t index = 0; index < lines_.size();) {
        wxString line = lines_[index];
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            settings = line.CmpNoCase("[" + sectionName + "]") == 0;
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
        lines_.push_back("[" + sectionName + "]");
        insertion = lines_.size();
    }
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insertion), key + wxString::Format("=%d", value));
}

bool SettingsStore::Save(bool explicitSave) {
    const OriginalSettings defaults;
    if (wxFileExists(path_) && !wxIsWritable(path_)) return false;
    // Merge the implemented fields into the current file, retaining other fields
    // and sections, including settings added by subsequent parity slices.
    if (!LoadLines()) return false;
    WriteBool("SaveSettings", values.saveSettings, defaults.saveSettings);
    if (values.saveSettings || explicitSave) {
        WriteInt("SaveRecentFiles",
                 static_cast<int>((static_cast<unsigned>(values.maxRecentFiles) << 1) |
                                  static_cast<unsigned>(values.saveRecentFiles)),
                 RecentFiles::MinimumCapacity << 1);
        WriteBool("WordWrap", values.wordWrap, defaults.wordWrap);
        WriteBool("AutoShrinkScrollWidth", values.autoShrinkScrollWidth, defaults.autoShrinkScrollWidth, "wxnotepad");
        WriteBool("ShowLineNumbers", values.showLineNumbers, defaults.showLineNumbers);
        WriteBool("ShowBookmarkMargin", values.showBookmarkMargin, defaults.showBookmarkMargin);
        WriteInt("ChangeHistoryMarker", values.changeHistoryMarker, defaults.changeHistoryMarker);
        WriteBool("MatchBraces", values.matchBraces, defaults.matchBraces);
        WriteBool("ShowCodeFolding", values.showCodeFolding, defaults.showCodeFolding);
        WriteBool("HighlightCurrentBlock", values.highlightCurrentBlock, defaults.highlightCurrentBlock);
        WriteBool("ViewWhiteSpace", values.viewWhiteSpace, defaults.viewWhiteSpace);
        WriteBool("ShowIndentGuides", values.showIndentGuides, defaults.showIndentGuides);
        WriteBool("ViewEOLs", values.viewEOLs, defaults.viewEOLs);
        WriteBool("ShowWordWrapSymbols", values.showWordWrapSymbols, defaults.showWordWrapSymbols);
        WriteInt("WordWrapSymbols", values.wordWrapSymbols, defaults.wordWrapSymbols);
        WriteBool("AutoSelect", values.autoSelect, defaults.autoSelect, "Styles");
        WriteInt("DefaultEncoding", values.encoding.defaultEncoding, defaults.encoding.defaultEncoding);
        WriteBool("SkipUnicodeDetection", values.encoding.skipUnicodeDetection, defaults.encoding.skipUnicodeDetection);
        WriteBool("LoadANSIasUTF8", values.encoding.loadANSIasUTF8, defaults.encoding.loadANSIasUTF8);
        WriteBool("LoadASCIIasUTF8", values.encoding.loadASCIIasUTF8, defaults.encoding.loadASCIIasUTF8);
        WriteBool("LoadNFOasOEM", values.encoding.loadNFOasOEM, defaults.encoding.loadNFOasOEM);
        WriteBool("NoEncodingTags", values.encoding.noEncodingTags, defaults.encoding.noEncodingTags);
        WriteInt("ZoomLevel", values.zoomLevel, defaults.zoomLevel);
        WriteInt("CaretStyle",
                 values.caret.lineWidth + 10 * values.caret.overstrikeBlock + 100 * values.caret.blockAfterSelection,
                 defaults.caret.lineWidth);
        WriteInt("CaretBlinkPeriod", values.caret.blinkPeriod, defaults.caret.blinkPeriod);
        WriteInt("HighlightCurrentLine", values.highlightCurrentLine, defaults.highlightCurrentLine);
        WriteInt("LongLinesLimit", values.longLinesLimit, defaults.longLinesLimit);
        WriteBool("MarkLongLines", values.markLongLines, defaults.markLongLines);
        WriteInt("LongLineMode", values.longLineMode, defaults.longLineMode);
        WriteBool("FixLineEndings", values.fixLineEndings, defaults.fixLineEndings);
        WriteBool("FixTrailingBlanks", values.fixTrailingBlanks, defaults.fixTrailingBlanks);
        WriteInt("FileWatchingMode", static_cast<int>(values.fileWatching.mode),
                 static_cast<int>(defaults.fileWatching.mode));
        WriteInt("FileCheckInterval", values.fileWatching.checkInterval, defaults.fileWatching.checkInterval,
                 "Settings2");
        WriteInt("AutoReloadTimeout", values.fileWatching.autoReloadDelay, defaults.fileWatching.autoReloadDelay,
                 "Settings2");
        WriteInt("FileWatchingOption", values.fileWatching.flags, defaults.fileWatching.flags);
        WriteBool("ResetFileWatching", values.fileWatching.resetOnNewFile, defaults.fileWatching.resetOnNewFile);
        WriteInt("SelectOption", values.selectOption, defaults.selectOption);
        WriteInt("AutoInsertMask", values.autoInsertMask, defaults.autoInsertMask);
        WriteInt("MarkOccurrences", values.markOccurrences, defaults.markOccurrences);
        WriteBool("SaveFindReplace", values.saveFindReplace, defaults.saveFindReplace);
        WriteInt("FindReplaceOption", values.findReplaceOption, defaults.findReplaceOption);
        WriteInt("FindReplaceFlag", values.saveFindReplace ? values.findReplaceFlag : defaults.findReplaceFlag,
                 defaults.findReplaceFlag);
        WriteInt("LineSelection", values.lineSelection, defaults.lineSelection);
        WriteInt("DefaultEOLMode", values.defaultEOLMode, defaults.defaultEOLMode);
        WriteBool("WarnLineEndings", values.warnLineEndings, defaults.warnLineEndings);
        WriteBool("TabsAsSpaces", values.globalTabs.tabsAsSpaces, defaults.globalTabs.tabsAsSpaces);
        WriteBool("TabIndents", values.tabIndents, defaults.tabIndents);
        WriteBool("AutoIndent", values.autoIndent, defaults.autoIndent);
        WriteBool("DetectIndentation", values.detectIndentation, defaults.detectIndentation);
        WriteInt("TabWidth", values.globalTabs.tabWidth, defaults.globalTabs.tabWidth);
        WriteInt("IndentWidth", values.globalTabs.indentWidth, defaults.globalTabs.indentWidth);
        WriteInt("BackspaceUnindents", values.backspaceUnindents, defaults.backspaceUnindents);
        for (size_t index = 0; index < values.schemeTabs.size(); ++index) {
            WriteSchemeTabs(index);
        }
    }
    // Original SaveAllSettings merges the MRUs on exit independently of
    // SaveSettings. Save Settings Now writes flags but not MRU sections.
    if (!explicitSave && values.saveFindReplace) {
        WriteHistory("Recent Find", values.recentFind);
        WriteHistory("Recent Replace", values.recentReplace);
    }
    // MRUList::MergeSave re-reads disk and adds this process oldest to newest.
    // It saves independently of SaveSettings and leaves stored history alone when disabled.
    if (!explicitSave && values.saveRecentFiles && !values.recentFiles.empty()) {
        auto merged = ReadRecentFiles();
        for (auto item = values.recentFiles.rbegin(); item != values.recentFiles.rend(); ++item)
            RecentFiles::Add(merged, *item, recentFileCapacity_);
        values.recentFiles = std::move(merged);
        WriteRecentFiles(values.recentFiles);
    }
    return SaveLines();
}

int SettingsStore::MessageSuppression(const wxString& key) {
    if (!LoadLines()) return 0;
    return ReadInt(key, 0, "Suppressed Messages");
}

bool SettingsStore::SuppressMessage(const wxString& key) {
    if (!LoadLines()) return false;
    WriteInt(key, 1, 0, "Suppressed Messages");
    return SaveLines();
}

void SettingsStore::WriteSchemeTabs(size_t index) {
    const auto& tabs = values.schemeTabs[index];
    const auto fallback = OriginalSettings{}.schemeTabs[index];
    const auto write = [this, index](const wxString& key, int value, int defaultValue) {
        WriteInt(key, value, defaultValue, SchemeSections[index]);
    };
    write("TabWidth", tabs.tabWidth, fallback.tabWidth);
    write("IndentWidth", tabs.indentWidth, fallback.indentWidth);
    write("TabsAsSpaces", tabs.tabsAsSpaces, fallback.tabsAsSpaces);
    write("UseGlobalTabSettings", tabs.useGlobal, fallback.useGlobal);
}

bool SettingsStore::SaveSchemeTabs(SyntaxLanguage language) {
    if (wxFileExists(path_) && !wxIsWritable(path_)) return false;
    if (!LoadLines()) return false;
    WriteSchemeTabs(static_cast<size_t>(language));
    return SaveLines();
}

bool SettingsStore::SaveLines() {
    wxString text(wxUniChar(0xFEFF));
    for (const auto& line : lines_)
        text += line + "\r\n";
    wxTempFile output(path_);
    return output.IsOpened() && output.Write(text, wxMBConvUTF16LE()) && output.Commit();
}
