#include "settings/settings_store.h"
#include "settings/style_profile_io.h"
#include <utility>
#include "search/search_limits.h"
#include "settings/recent_files.h"
#include <ScintillaTypes.h>
#include <wx/ffile.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <string>
#include <algorithm>
#include <cctype>
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
    const wxString programPath = executable.GetFullPath();
    if (programPath.StartsWith("C:\\Program Files") || programPath.Contains("WinGet") ||
        programPath.Contains("hocolatey")) {
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local))) {
            directory = wxFileName(local, "Notepad4").GetFullPath();
            CoTaskMemFree(local);
        }
    }
#else
    // Native Linux installation directories map to the XDG user config directory.
    const wxString programPath = executable.GetFullPath();
    if (programPath.StartsWith("/usr/") || programPath.StartsWith("/opt/") || programPath.StartsWith("/app/") ||
        programPath.StartsWith("/snap/") || programPath.StartsWith("/bin/")) {
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
wxString ParseOriginalIniString(wxString value, size_t maxUtf16Units) {
    value.Trim().Trim(false);
    if (value.length() >= 2 &&
        ((value.StartsWith("\"") && value.EndsWith("\"")) || (value.StartsWith("'") && value.EndsWith("'"))))
        value = value.Mid(1, value.length() - 2);
    // IniGetString's original WCHAR buffers reserve one UTF-16 unit for NUL.
    wxString truncated;
    size_t units = 0;
    for (const wxUniChar character : value) {
        if (units == maxUtf16Units) break;
        const size_t count = character.GetValue() >= 0x10000 ? 2 : 1;
        if (units + count > maxUtf16Units) {
            truncated += wxUniChar(0xfffd);
            break;
        }
        truncated += character;
        units += count;
    }
    return truncated;
}
} // namespace

SettingsStore::SettingsStore() : SettingsStore(ConfigurationPath()) {
}

int SettingsStore::OriginalRunCommandWidth(int screenWidth, int screenHeight) const {
    const auto section = wxString::Format("Window Position %dx%d", screenWidth, screenHeight);
    const int matching = document_.ReadInt("RunDlgSizeX", 0, section);
    if (matching > 0) return matching;
    // Imported Windows monitor dimensions may not exist on the current desktop.
    // Reuse the first saved original width without creating a new monitor schema.
    for (const auto& name : document_.SectionNames()) {
        if (!name.StartsWith("Window Position ")) continue;
        const int width = document_.ReadInt("RunDlgSizeX", 0, name);
        if (width > 0) return width;
    }
    return 0;
}

SettingsStore::SettingsStore(const wxString& path) : document_(path) {
    const OriginalSettings defaults;
    document_.Load();
    values.saveSettings = document_.ReadBool("SaveSettings", defaults.saveSettings);
    values.autoSave.flags = document_.ReadInt("AutoSaveOption", defaults.autoSave.flags);
    values.autoSave.periodMilliseconds =
        static_cast<uint32_t>(document_.ReadInt("AutoSavePeriod", defaults.autoSave.periodMilliseconds));
    values.saveBeforeRunningTools = document_.ReadBool("SaveBeforeRunningTools", defaults.saveBeforeRunningTools);
    const auto openWith = document_.ReadString("OpenWithDir", {});
    if (!openWith.empty()) values.openWithDirectory = RecentFiles::ExpandPath(openWith);
    values.runCommandWidthDIP = std::max(0, document_.ReadInt("RunDlgSizeX", defaults.runCommandWidthDIP, "wxnotepad"));
    auto& print = values.printing;
    print.header = static_cast<PrintHeader>(
        std::clamp(document_.ReadInt("PrintHeader", std::to_underlying(print.header)),
                   std::to_underlying(PrintHeader::FilenameAndDateTime), std::to_underlying(PrintHeader::Blank)));
    print.footer = static_cast<PrintFooter>(
        std::clamp(document_.ReadInt("PrintFooter", std::to_underlying(print.footer)),
                   std::to_underlying(PrintFooter::PageNumber), std::to_underlying(PrintFooter::Blank)));
    print.colour = static_cast<Scintilla::PrintOption>(std::clamp(
        document_.ReadInt("PrintColorMode", std::to_underlying(print.colour)),
        std::to_underlying(Scintilla::PrintOption::Normal), std::to_underlying(Scintilla::PrintOption::ScreenColours)));
    print.zoom =
        std::clamp(document_.ReadInt("PrintZoom", print.zoom), Scintilla::MinZoomLevel, Scintilla::MaxZoomLevel);
    for (size_t index = 0; index < PrintMarginKeys.size(); ++index)
        print.margins[index] = std::max(document_.ReadInt(PrintMarginKeys[index], print.margins[index]), -1);
    values.uiLanguage = UILanguage::Normalize(document_.ReadInt("UILanguage", defaults.uiLanguage, "Settings2"));
    const int recent = document_.ReadInt("SaveRecentFiles", RecentFiles::MinimumCapacity << 1);
    values.saveRecentFiles = (recent & 1) != 0;
    values.maxRecentFiles = std::max(recent >> 1, RecentFiles::MinimumCapacity);
    recentFileCapacity_ = values.maxRecentFiles;
    values.relativeFileMRU = document_.ReadBool("RelativeFileMRU", true, "Settings2");
    values.portableMyDocs = document_.ReadBool("PortableMyDocs", values.relativeFileMRU, "Settings2");
    if (values.saveRecentFiles) values.recentFiles = ReadRecentFiles();
    values.showMenu = document_.ReadBool("ShowMenu", defaults.showMenu);
    values.showToolbar = document_.ReadBool("ShowToolbar", defaults.showToolbar);
    values.autoScaleToolbar = document_.ReadInt("AutoScaleToolbar", defaults.autoScaleToolbar);
    values.toolbarButtons = document_.ReadString("ToolbarButtons", defaults.toolbarButtons);
    values.showStatusbar = document_.ReadBool("ShowStatusbar", defaults.showStatusbar);
    values.fullScreenMode = document_.ReadInt("FullScreenMode", defaults.fullScreenMode);
    values.escapeFunction = static_cast<EscapeFunction>(
        std::clamp(document_.ReadInt("EscFunction", std::to_underlying(defaults.escapeFunction)),
                   std::to_underlying(EscapeFunction::None), std::to_underlying(EscapeFunction::Exit)));
    values.pathNameFormat = static_cast<TitlePathNameFormat>(std::clamp(
        document_.ReadInt("PathNameFormat", std::to_underlying(defaults.pathNameFormat)),
        std::to_underlying(TitlePathNameFormat::NameOnly), std::to_underlying(TitlePathNameFormat::FullPath)));
    values.wordWrap = document_.ReadBool("WordWrap", defaults.wordWrap);
    values.wordWrapMode =
        std::clamp(document_.ReadInt("WordWrapMode", defaults.wordWrapMode), std::to_underlying(Scintilla::Wrap::Word),
                   std::to_underlying(Scintilla::Wrap::Auto));
    values.wordWrapIndent = static_cast<WrapContinuationIndent>(std::clamp(
        document_.ReadInt("WordWrapIndent", std::to_underlying(defaults.wordWrapIndent)),
        std::to_underlying(WrapContinuationIndent::None), std::to_underlying(WrapContinuationIndent::DeepIndent)));
    values.wordWrapSelectSubLine = document_.ReadBool("WordWrapSelectSubLine", defaults.wordWrapSelectSubLine);
    values.endAtLastLine = std::clamp(document_.ReadInt("EndAtLastLine", defaults.endAtLastLine), 0, 4);
    values.autoShrinkScrollWidth =
        document_.ReadBool("AutoShrinkScrollWidth", defaults.autoShrinkScrollWidth, "wxnotepad");
    values.showLineNumbers = document_.ReadBool("ShowLineNumbers", defaults.showLineNumbers);
    values.showBookmarkMargin = document_.ReadBool("ShowBookmarkMargin", defaults.showBookmarkMargin);
    values.changeHistoryMarker = document_.ReadInt("ChangeHistoryMarker", defaults.changeHistoryMarker);
    values.matchBraces = document_.ReadBool("MatchBraces", defaults.matchBraces);
    values.showCodeFolding = document_.ReadBool("ShowCodeFolding", defaults.showCodeFolding);
    values.highlightCurrentBlock = document_.ReadBool("HighlightCurrentBlock", defaults.highlightCurrentBlock);
    values.viewWhiteSpace = document_.ReadBool("ViewWhiteSpace", defaults.viewWhiteSpace);
    values.showIndentGuides = document_.ReadBool("ShowIndentGuides", defaults.showIndentGuides);
    values.simpleIndentGuides = document_.ReadBool("SimpleIndentGuides", defaults.simpleIndentGuides, "Settings2");
    values.viewEOLs = document_.ReadBool("ViewEOLs", defaults.viewEOLs);
    values.showUnicodeControlCharacter =
        document_.ReadBool("ShowUnicodeControlCharacter", defaults.showUnicodeControlCharacter);
    const int callTipMode = document_.ReadInt("ShowCallTip", std::to_underlying(defaults.showCallTip));
    if (callTipMode >= std::to_underlying(ColorCallTipMode::Off) &&
        callTipMode <= std::to_underlying(ColorCallTipMode::ABGR))
        values.showCallTip = static_cast<ColorCallTipMode>(callTipMode);
    values.showWordWrapSymbols = document_.ReadBool("ShowWordWrapSymbols", defaults.showWordWrapSymbols);
    const int wrapSymbols = std::clamp(document_.ReadInt("WordWrapSymbols", defaults.wordWrapSymbols), 0, 32);
    values.wordWrapSymbols = std::min(wrapSymbols % 10, 2) + (wrapSymbols / 10) * 10;
    values.autoSelect = document_.ReadBool("AutoSelect", defaults.autoSelect, "Styles");
    values.defaultScheme = document_.ReadInt("DefaultScheme", defaults.defaultScheme, "Styles");
    values.favoriteSchemes = document_.ReadString("FavoriteSchemes", {}, "Styles");
    values.noCgiGuess = document_.ReadBool("NoCGIGuess", defaults.noCgiGuess, "Settings2");
    values.noHtmlGuess = document_.ReadBool("NoHTMLGuess", defaults.noHtmlGuess, "Settings2");
    values.fixLineEndings = document_.ReadBool("FixLineEndings", defaults.fixLineEndings);
    values.fixTrailingBlanks = document_.ReadBool("FixTrailingBlanks", defaults.fixTrailingBlanks);
    values.fileWatching.mode = static_cast<FileWatchingMode>(
        std::clamp(document_.ReadInt("FileWatchingMode", std::to_underlying(defaults.fileWatching.mode)),
                   std::to_underlying(FileWatchingMode::None), std::to_underlying(FileWatchingMode::AutoReload)));
    values.fileWatching.checkInterval =
        document_.ReadInt("FileCheckInterval", defaults.fileWatching.checkInterval, "Settings2");
    values.fileWatching.autoReloadDelay =
        document_.ReadInt("AutoReloadTimeout", defaults.fileWatching.autoReloadDelay, "Settings2");
    values.fileWatching.flags = document_.ReadInt("FileWatchingOption", defaults.fileWatching.flags);
    values.fileWatching.resetOnNewFile = document_.ReadBool("ResetFileWatching", defaults.fileWatching.resetOnNewFile);
    values.encoding.defaultEncoding =
        NormalizeEncodingSetting(document_.ReadInt("DefaultEncoding", defaults.encoding.defaultEncoding));
    values.encoding.skipUnicodeDetection =
        document_.ReadBool("SkipUnicodeDetection", defaults.encoding.skipUnicodeDetection);
    values.encoding.loadANSIasUTF8 = document_.ReadBool("LoadANSIasUTF8", defaults.encoding.loadANSIasUTF8);
    values.encoding.loadASCIIasUTF8 = document_.ReadBool("LoadASCIIasUTF8", defaults.encoding.loadASCIIasUTF8);
    values.encoding.loadNFOasOEM = document_.ReadBool("LoadNFOasOEM", defaults.encoding.loadNFOasOEM);
    values.encoding.noEncodingTags = document_.ReadBool("NoEncodingTags", defaults.encoding.noEncodingTags);
    values.encoding.noFileVariables =
        document_.ReadBool("NoFileVariables", defaults.encoding.noFileVariables, "Settings2");
    values.zoomLevel = std::clamp(document_.ReadInt("ZoomLevel", defaults.zoomLevel), Scintilla::MinZoomLevel,
                                  Scintilla::MaxZoomLevel);
    values.renderingTechnology = std::clamp(document_.ReadInt("RenderingTechnology", defaults.renderingTechnology),
                                            std::to_underlying(Scintilla::Technology::Default),
                                            std::to_underlying(Scintilla::Technology::DirectWrite1));
    values.fontQuality = std::clamp(document_.ReadInt("FontQuality", defaults.fontQuality),
                                    std::to_underlying(Scintilla::FontQuality::QualityDefault),
                                    std::to_underlying(Scintilla::FontQuality::QualityLcdOptimized));
    int caretStyle = document_.ReadInt("CaretStyle", defaults.caret.lineWidth);
    values.caret.blockAfterSelection = caretStyle >= 100;
    caretStyle %= 100;
    values.caret.overstrikeBlock = caretStyle >= 10;
    values.caret.lineWidth = std::clamp(caretStyle % 10, 0, 3);
    values.caret.blinkPeriod = document_.ReadInt("CaretBlinkPeriod", defaults.caret.blinkPeriod);
    const int currentLine = document_.ReadInt("HighlightCurrentLine", defaults.highlightCurrentLine);
    values.highlightCurrentLine = std::clamp(currentLine % 10, 0, 2) + (currentLine >= 10 ? 10 : 0);
    values.longLinesLimit = std::clamp(document_.ReadInt("LongLinesLimit", defaults.longLinesLimit), 0,
                                       OriginalSettingLimits::MaxLongLineColumn);
    values.markLongLines = document_.ReadBool("MarkLongLines", defaults.markLongLines);
    values.longLineMode = std::clamp(document_.ReadInt("LongLineMode", defaults.longLineMode), 1, 2);
    values.selectOption = document_.ReadInt("SelectOption", defaults.selectOption);
    auto& completion = values.completion;
    const auto& defaultCompletion = defaults.completion;
    completion.completeOption = document_.ReadInt("AutoCompleteOption", defaultCompletion.completeOption);
    const int scopes = document_.ReadInt("AutoCompleteScope", WordCompletionLimits::DefaultScopes);
    completion.completeScope = scopes & WordCompletionLimits::ScopeMask;
    completion.scanWordScope = scopes >> WordCompletionLimits::ScanScopeShift;
    completion.scanWordsTimeout =
        std::max(document_.ReadInt("AutoCScanWordsTimeout", defaultCompletion.scanWordsTimeout),
                 WordCompletionLimits::MinScanTimeoutMs);
    completion.ignoreCase = document_.ReadBool("AutoCIgnoreCase", defaultCompletion.ignoreCase);
    completion.latexInputMethod = document_.ReadBool("LaTeXInputMethod", defaultCompletion.latexInputMethod);
    completion.visibleItemCount =
        std::max(document_.ReadInt("AutoCVisibleItemCount", defaultCompletion.visibleItemCount),
                 WordCompletionLimits::MinVisibleRows);
    completion.minWordLength = std::max(document_.ReadInt("AutoCMinWordLength", defaultCompletion.minWordLength),
                                        WordCompletionLimits::MinWordLength);
    completion.minNumberLength = std::max(document_.ReadInt("AutoCMinNumberLength", defaultCompletion.minNumberLength),
                                          WordCompletionLimits::MinNumberLength);
    completion.fillUpMask = document_.ReadInt("AutoCFillUpMask", defaultCompletion.fillUpMask);
    const auto punctuation = document_.ReadString("AutoCFillUpPunctuation", defaultCompletion.fillUpPunctuation)
                                 .Left(WordCompletionLimits::MaxPunctuationCharacters);
    completion.fillUpPunctuation.clear();
    for (const wxUniChar ch : punctuation)
        if (ch.GetValue() < 128 && std::ispunct(static_cast<unsigned char>(ch.GetValue())))
            completion.fillUpPunctuation += ch;
    values.autoInsertMask = document_.ReadInt("AutoInsertMask", defaults.autoInsertMask);
    values.markOccurrences = document_.ReadInt("MarkOccurrences", defaults.markOccurrences);
    values.saveFindReplace = document_.ReadBool("SaveFindReplace", defaults.saveFindReplace);
    values.findReplaceOption = document_.ReadInt("FindReplaceOption", defaults.findReplaceOption);
    if (values.saveFindReplace) {
        values.findReplaceFlag = document_.ReadInt("FindReplaceFlag", defaults.findReplaceFlag);
        values.recentFind = document_.ReadHistory("Recent Find", SearchLimits::HistoryEntries);
        values.recentReplace = document_.ReadHistory("Recent Replace", SearchLimits::HistoryEntries);
    }
    values.lineSelection = document_.ReadInt("LineSelection", defaults.lineSelection);
    values.defaultEOLMode = std::clamp(document_.ReadInt("DefaultEOLMode", defaults.defaultEOLMode), 0, 2);
    values.warnLineEndings = document_.ReadBool("WarnLineEndings", defaults.warnLineEndings);
    values.globalTabs.tabWidth =
        std::clamp(document_.ReadInt("TabWidth", defaults.globalTabs.tabWidth), 1, OriginalSettingLimits::MaxTabWidth);
    values.globalTabs.indentWidth = std::clamp(document_.ReadInt("IndentWidth", defaults.globalTabs.indentWidth), 0,
                                               OriginalSettingLimits::MaxTabWidth);
    values.globalTabs.tabsAsSpaces = document_.ReadBool("TabsAsSpaces", defaults.globalTabs.tabsAsSpaces);
    values.tabIndents = document_.ReadBool("TabIndents", defaults.tabIndents);
    values.autoIndent = document_.ReadBool("AutoIndent", defaults.autoIndent);
    values.detectIndentation = document_.ReadBool("DetectIndentation", defaults.detectIndentation);
    values.backspaceUnindents =
        static_cast<uint8_t>(document_.ReadInt("BackspaceUnindents", defaults.backspaceUnindents));
    values.dateTimeShort = ReadDateTimeFormat("DateTimeShort");
    values.dateTimeLong = ReadDateTimeFormat("DateTimeLong");
    for (size_t index = 0; index < values.schemeTabs.size(); ++index) {
        const auto language = static_cast<SyntaxLanguage>(index);
        if (SyntaxSettingsLanguage(language) != language) continue;
        const auto* section = SyntaxSettingsSection(language);
        auto& tabs = values.schemeTabs[index];
        tabs.tabWidth =
            std::clamp(document_.ReadInt("TabWidth", tabs.tabWidth, section), 1, OriginalSettingLimits::MaxTabWidth);
        tabs.indentWidth = std::clamp(document_.ReadInt("IndentWidth", tabs.indentWidth, section), 0,
                                      OriginalSettingLimits::MaxTabWidth);
        tabs.tabsAsSpaces = document_.ReadInt("TabsAsSpaces", tabs.tabsAsSpaces, section) != 0;
        tabs.useGlobal = document_.ReadInt("UseGlobalTabSettings", tabs.useGlobal, section) != 0;
    }
    const auto theme = static_cast<StyleTheme>(std::clamp(document_.ReadInt("StyleTheme", 0, "Styles"), 0, 1));
    values.fonts = LoadStyleProfile(theme);
    loadedFonts_ = values.fonts;
    for (const auto& scheme : SyntaxCatalog()) {
        if (SyntaxSettingsLanguage(scheme.language) != scheme.language) continue;
        if (const auto extension = document_.FindValue(SyntaxSettingsSection(scheme.language), "File Extensions");
            extension && !extension->empty())
            values.schemeExtensions[scheme.language] = *extension;
    }
    loadedExtensions_ = values.schemeExtensions;
}

void SettingsStore::WriteExtensions() {
    for (const auto& scheme : SyntaxCatalog()) {
        if (SyntaxSettingsLanguage(scheme.language) != scheme.language) continue;
        const auto value = SyntaxExtensionsFor(scheme.language, values.schemeExtensions);
        if (value != SyntaxExtensionsFor(scheme.language, loadedExtensions_))
            document_.WriteString(SyntaxSettingsSection(scheme.language), value, SyntaxExtensionsFor(scheme.language),
                                  "File Extensions");
    }
}

wxString SettingsStore::DarkProfilePath() const {
    return wxFileName(wxFileName(document_.Path()).GetPath(), "Notepad4 DarkTheme.ini").GetFullPath();
}

FontSettings SettingsStore::LoadStyleProfile(StyleTheme theme) {
    return StyleProfileIO::Load(theme == StyleTheme::Dark ? DarkProfilePath() : Path(), theme, values.fonts);
}

FontSettings SettingsStore::ResetStyles() {
    return values.fonts.theme == StyleTheme::Dark ? LoadStyleProfile(StyleTheme::Dark) : FontSettings{};
}

bool SettingsStore::ImportStyles(const wxString& path) {
    return StyleProfileIO::Import(path, values.fonts, values.schemeExtensions);
}

bool SettingsStore::ExportStyles(const wxString& path) {
    return StyleProfileIO::Export(path, values.fonts, values.schemeExtensions);
}

bool SettingsStore::SaveStyleProfile() {
    if (values.fonts == loadedFonts_) return true;
    if (values.fonts.theme == StyleTheme::Default) {
        StyleProfileIO::WriteChanges(document_, values.fonts, loadedFonts_);
        return true;
    }
    IniDocument profile(DarkProfilePath());
    if (!profile.Load(wxString::FromUTF8(OriginalDarkProfile()))) return false;
    StyleProfileIO::WriteChanges(profile, values.fonts, loadedFonts_);
    return profile.Save();
}

bool SettingsStore::SwitchStyleTheme(StyleTheme theme) {
    if (theme == values.fonts.theme) return true;
    // Style_OnStyleThemeChanged saves all dirty styles before changing identity,
    // including when the ordinary SaveSettings option is disabled.
    bool saved = true;
    if (values.fonts != loadedFonts_ || values.schemeExtensions != loadedExtensions_) {
        // SaveSettingsNow(true, true) calls Style_Save only. Ordinary view/tool
        // preferences remain unsaved when Save Settings On Exit is disabled.
        const wxLogNull noGuiLog;
        saved = (!wxFileExists(document_.Path()) || wxIsWritable(document_.Path())) && document_.Load();
        if (saved) {
            document_.WriteInt("StyleTheme", std::to_underlying(values.fonts.theme), 0, "Styles");
            document_.WriteInt("DefaultScheme", values.defaultScheme, 0, "Styles");
            document_.WriteBool("AutoSelect", values.autoSelect, true, "Styles");
            document_.WriteString("FavoriteSchemes", values.favoriteSchemes, {}, "Styles");
            WriteExtensions();
            saved = SaveStyleProfile() && document_.Save();
        }
    }
    // The original still changes theme after reporting a style-save failure.
    values.fonts = LoadStyleProfile(theme);
    loadedFonts_ = values.fonts;
    if (saved) loadedExtensions_ = values.schemeExtensions;
    return saved;
}

wxString SettingsStore::ReadDateTimeFormat(const wxString& key) const {
    const auto value = document_.FindValue(key, "Settings2", IniDocument::KeyCase::Insensitive);
    constexpr size_t MaxDateTimeFormatUtf16Units = 255;
    return value ? ParseOriginalIniString(*value, MaxDateTimeFormatUtf16Units) : wxString{};
}

wxString SettingsStore::SelectionActionTemplate(SelectionAction action) {
    constexpr std::array keys{"GoogleSearchUrl", "BingSearchUrl", "WikiSearchUrl", "CustomAction1", "CustomAction2"};
    const wxString key(keys[std::to_underlying(action)]);
    // EditSelectionAction reads these fields at invocation, using WCHAR[MAX_PATH * 2].
    constexpr size_t TemplateUtf16Capacity = 520;
#ifdef __WXMSW__
    std::array<wchar_t, TemplateUtf16Capacity> value{};
    GetPrivateProfileStringW(L"Settings2", key.wc_str(), L"", value.data(), static_cast<DWORD>(value.size()),
                             document_.Path().wc_str());
    return wxString(value.data());
#else
    const wxLogNull noGuiLog;
    if (!document_.Load()) return {};
    const auto value = document_.FindValue(key, "Settings2", IniDocument::KeyCase::Insensitive);
    return value ? ParseOriginalIniString(*value, TemplateUtf16Capacity - 1) : wxString{};
#endif
}

std::vector<wxString> SettingsStore::ReadRecentFiles() const {
    auto history = document_.ReadHistory("Recent Files", recentFileCapacity_, true);
    for (auto& path : history)
        path = RecentFiles::ExpandPath(path);
    return history;
}

void SettingsStore::WriteRecentFiles(const std::vector<wxString>& history) {
    std::vector<wxString> portable;
    portable.reserve(history.size());
    for (const auto& path : history)
        portable.push_back(RecentFiles::PortablePath(path, values.relativeFileMRU, values.portableMyDocs));
    document_.ReplaceHistorySection("Recent Files", portable);
}

void SettingsStore::AddRecentFile(const wxString& path) {
    RecentFiles::Add(values.recentFiles, path, recentFileCapacity_);
}

void SettingsStore::ClearRecentFiles(const wxString& currentPath) {
    values.recentFiles.clear();
    if (values.saveRecentFiles && document_.Load()) {
        WriteRecentFiles({});
        document_.Save();
    }
    AddRecentFile(currentPath);
}

void SettingsStore::RemoveRecentFile(size_t index) {
    if (index >= values.recentFiles.size()) return;
    const wxString removed = values.recentFiles[index];
    if (document_.Load()) {
        auto stored = ReadRecentFiles();
        std::erase_if(stored, [&removed](const auto& path) { return RecentFiles::Equal(path, removed); });
        WriteRecentFiles(stored);
        document_.Save();
    }
    values.recentFiles.erase(values.recentFiles.begin() + static_cast<std::ptrdiff_t>(index));
}

void SettingsStore::WriteHistory(const wxString& sectionName, const std::vector<wxString>& history) {
    if (history.empty()) return;
    auto merged = document_.ReadHistory(sectionName, SearchLimits::HistoryEntries);
    for (auto item = history.rbegin(); item != history.rend(); ++item) {
        std::erase(merged, *item);
        merged.insert(merged.begin(), *item);
    }
    if (merged.size() > SearchLimits::HistoryEntries) merged.resize(SearchLimits::HistoryEntries);
    document_.ReplaceHistorySection(sectionName, merged);
}

bool SettingsStore::Save(bool explicitSave) {
    const OriginalSettings defaults;
    if (wxFileExists(document_.Path()) && !wxIsWritable(document_.Path())) return false;
    // Merge the implemented fields into the current file, retaining other fields
    // and sections, including settings added by subsequent parity slices.
    if (!document_.Load()) return false;
    document_.WriteBool("SaveSettings", values.saveSettings, defaults.saveSettings);
    if (values.saveSettings || explicitSave) {
        if (!SaveStyleProfile()) return false;
        WriteExtensions();
        document_.WriteInt("StyleTheme", std::to_underlying(values.fonts.theme), 0, "Styles");
        document_.WriteInt("AutoSaveOption", values.autoSave.flags, defaults.autoSave.flags);
        document_.WriteInt("AutoSavePeriod", static_cast<int32_t>(values.autoSave.periodMilliseconds),
                           defaults.autoSave.periodMilliseconds);
        document_.WriteBool("SaveBeforeRunningTools", values.saveBeforeRunningTools, defaults.saveBeforeRunningTools);
        document_.WriteString("OpenWithDir",
                              values.openWithDirectory.empty()
                                  ? wxString{}
                                  : RecentFiles::PortablePath(values.openWithDirectory, true, values.portableMyDocs,
                                                              RecentFiles::PathKind::Directory),
                              {});
        document_.WriteInt("RunDlgSizeX", values.runCommandWidthDIP, defaults.runCommandWidthDIP, "wxnotepad");
        const auto& print = values.printing;
        const auto& defaultPrint = defaults.printing;
        document_.WriteInt("PrintHeader", std::to_underlying(print.header), std::to_underlying(defaultPrint.header));
        document_.WriteInt("PrintFooter", std::to_underlying(print.footer), std::to_underlying(defaultPrint.footer));
        document_.WriteInt("PrintColorMode", std::to_underlying(print.colour), std::to_underlying(defaultPrint.colour));
        document_.WriteInt("PrintZoom", print.zoom, defaultPrint.zoom);
        for (size_t index = 0; index < PrintMarginKeys.size(); ++index)
            document_.WriteInt(PrintMarginKeys[index], print.margins[index], defaultPrint.margins[index]);
        document_.WriteInt("SaveRecentFiles",
                           static_cast<int>((static_cast<unsigned>(values.maxRecentFiles) << 1) |
                                            static_cast<unsigned>(values.saveRecentFiles)),
                           RecentFiles::MinimumCapacity << 1);
        document_.WriteBool("ShowMenu", values.showMenu, defaults.showMenu);
        document_.WriteBool("ShowToolbar", values.showToolbar, defaults.showToolbar);
        document_.WriteInt("AutoScaleToolbar", values.autoScaleToolbar, defaults.autoScaleToolbar);
        document_.WriteString("ToolbarButtons", values.toolbarButtons, defaults.toolbarButtons);
        document_.WriteBool("ShowStatusbar", values.showStatusbar, defaults.showStatusbar);
        document_.WriteInt("FullScreenMode", values.fullScreenMode, defaults.fullScreenMode);
        document_.WriteInt("EscFunction", std::to_underlying(values.escapeFunction),
                           std::to_underlying(defaults.escapeFunction));
        document_.WriteInt("PathNameFormat", std::to_underlying(values.pathNameFormat),
                           std::to_underlying(defaults.pathNameFormat));
        document_.WriteBool("WordWrap", values.wordWrap, defaults.wordWrap);
        document_.WriteInt("WordWrapMode", values.wordWrapMode, defaults.wordWrapMode);
        document_.WriteInt("WordWrapIndent", std::to_underlying(values.wordWrapIndent),
                           std::to_underlying(defaults.wordWrapIndent));
        document_.WriteBool("WordWrapSelectSubLine", values.wordWrapSelectSubLine, defaults.wordWrapSelectSubLine);
        document_.WriteInt("EndAtLastLine", values.endAtLastLine, defaults.endAtLastLine);
        document_.WriteBool("AutoShrinkScrollWidth", values.autoShrinkScrollWidth, defaults.autoShrinkScrollWidth,
                            "wxnotepad");
        document_.WriteBool("ShowLineNumbers", values.showLineNumbers, defaults.showLineNumbers);
        document_.WriteBool("ShowBookmarkMargin", values.showBookmarkMargin, defaults.showBookmarkMargin);
        document_.WriteInt("ChangeHistoryMarker", values.changeHistoryMarker, defaults.changeHistoryMarker);
        document_.WriteBool("MatchBraces", values.matchBraces, defaults.matchBraces);
        document_.WriteBool("ShowCodeFolding", values.showCodeFolding, defaults.showCodeFolding);
        document_.WriteBool("HighlightCurrentBlock", values.highlightCurrentBlock, defaults.highlightCurrentBlock);
        document_.WriteBool("ViewWhiteSpace", values.viewWhiteSpace, defaults.viewWhiteSpace);
        document_.WriteBool("ShowIndentGuides", values.showIndentGuides, defaults.showIndentGuides);
        document_.WriteBool("ViewEOLs", values.viewEOLs, defaults.viewEOLs);
        document_.WriteBool("ShowUnicodeControlCharacter", values.showUnicodeControlCharacter,
                            defaults.showUnicodeControlCharacter);
        document_.WriteInt("ShowCallTip", std::to_underlying(values.showCallTip),
                           std::to_underlying(defaults.showCallTip));
        document_.WriteBool("ShowWordWrapSymbols", values.showWordWrapSymbols, defaults.showWordWrapSymbols);
        document_.WriteInt("WordWrapSymbols", values.wordWrapSymbols, defaults.wordWrapSymbols);
        document_.WriteBool("AutoSelect", values.autoSelect, defaults.autoSelect, "Styles");
        document_.WriteInt("DefaultScheme", values.defaultScheme, defaults.defaultScheme, "Styles");
        document_.WriteString("FavoriteSchemes", values.favoriteSchemes, {}, "Styles");
        document_.WriteInt("DefaultEncoding", values.encoding.defaultEncoding, defaults.encoding.defaultEncoding);
        document_.WriteBool("SkipUnicodeDetection", values.encoding.skipUnicodeDetection,
                            defaults.encoding.skipUnicodeDetection);
        document_.WriteBool("LoadANSIasUTF8", values.encoding.loadANSIasUTF8, defaults.encoding.loadANSIasUTF8);
        document_.WriteBool("LoadASCIIasUTF8", values.encoding.loadASCIIasUTF8, defaults.encoding.loadASCIIasUTF8);
        document_.WriteBool("LoadNFOasOEM", values.encoding.loadNFOasOEM, defaults.encoding.loadNFOasOEM);
        document_.WriteBool("NoEncodingTags", values.encoding.noEncodingTags, defaults.encoding.noEncodingTags);
        document_.WriteInt("ZoomLevel", values.zoomLevel, defaults.zoomLevel);
#ifdef __WXMSW__
        document_.WriteInt("RenderingTechnology", values.renderingTechnology, defaults.renderingTechnology);
        document_.WriteInt("FontQuality", values.fontQuality, defaults.fontQuality);
#endif
        document_.WriteInt("CaretStyle",
                           values.caret.lineWidth + (values.caret.overstrikeBlock ? 10 : 0) +
                               (values.caret.blockAfterSelection ? 100 : 0),
                           defaults.caret.lineWidth);
        document_.WriteInt("CaretBlinkPeriod", values.caret.blinkPeriod, defaults.caret.blinkPeriod);
        document_.WriteInt("HighlightCurrentLine", values.highlightCurrentLine, defaults.highlightCurrentLine);
        document_.WriteInt("LongLinesLimit", values.longLinesLimit, defaults.longLinesLimit);
        document_.WriteBool("MarkLongLines", values.markLongLines, defaults.markLongLines);
        document_.WriteInt("LongLineMode", values.longLineMode, defaults.longLineMode);
        document_.WriteBool("FixLineEndings", values.fixLineEndings, defaults.fixLineEndings);
        document_.WriteBool("FixTrailingBlanks", values.fixTrailingBlanks, defaults.fixTrailingBlanks);
        document_.WriteInt("FileWatchingMode", std::to_underlying(values.fileWatching.mode),
                           std::to_underlying(defaults.fileWatching.mode));
        document_.WriteInt("FileCheckInterval", values.fileWatching.checkInterval, defaults.fileWatching.checkInterval,
                           "Settings2");
        document_.WriteInt("AutoReloadTimeout", values.fileWatching.autoReloadDelay,
                           defaults.fileWatching.autoReloadDelay, "Settings2");
        document_.WriteInt("FileWatchingOption", values.fileWatching.flags, defaults.fileWatching.flags);
        document_.WriteBool("ResetFileWatching", values.fileWatching.resetOnNewFile,
                            defaults.fileWatching.resetOnNewFile);
        document_.WriteInt("SelectOption", values.selectOption, defaults.selectOption);
        const auto& completion = values.completion;
        const auto& defaultCompletion = defaults.completion;
        document_.WriteInt("AutoCompleteOption", completion.completeOption, defaultCompletion.completeOption);
        document_.WriteInt("AutoCompleteScope",
                           completion.completeScope |
                               (completion.scanWordScope << WordCompletionLimits::ScanScopeShift),
                           WordCompletionLimits::DefaultScopes);
        document_.WriteInt("AutoCScanWordsTimeout", completion.scanWordsTimeout, defaultCompletion.scanWordsTimeout);
        document_.WriteBool("AutoCIgnoreCase", completion.ignoreCase, defaultCompletion.ignoreCase);
        document_.WriteBool("LaTeXInputMethod", completion.latexInputMethod, defaultCompletion.latexInputMethod);
        document_.WriteInt("AutoCVisibleItemCount", completion.visibleItemCount, defaultCompletion.visibleItemCount);
        document_.WriteInt("AutoCMinWordLength", completion.minWordLength, defaultCompletion.minWordLength);
        document_.WriteInt("AutoCMinNumberLength", completion.minNumberLength, defaultCompletion.minNumberLength);
        document_.WriteInt("AutoCFillUpMask", completion.fillUpMask, defaultCompletion.fillUpMask);
        document_.WriteString("AutoCFillUpPunctuation", completion.fillUpPunctuation,
                              defaultCompletion.fillUpPunctuation);
        document_.WriteInt("AutoInsertMask", values.autoInsertMask, defaults.autoInsertMask);
        document_.WriteInt("MarkOccurrences", values.markOccurrences, defaults.markOccurrences);
        document_.WriteBool("SaveFindReplace", values.saveFindReplace, defaults.saveFindReplace);
        document_.WriteInt("FindReplaceOption", values.findReplaceOption, defaults.findReplaceOption);
        document_.WriteInt("FindReplaceFlag",
                           values.saveFindReplace ? values.findReplaceFlag : defaults.findReplaceFlag,
                           defaults.findReplaceFlag);
        document_.WriteInt("LineSelection", values.lineSelection, defaults.lineSelection);
        document_.WriteInt("DefaultEOLMode", values.defaultEOLMode, defaults.defaultEOLMode);
        document_.WriteBool("WarnLineEndings", values.warnLineEndings, defaults.warnLineEndings);
        document_.WriteBool("TabsAsSpaces", values.globalTabs.tabsAsSpaces, defaults.globalTabs.tabsAsSpaces);
        document_.WriteBool("TabIndents", values.tabIndents, defaults.tabIndents);
        document_.WriteBool("AutoIndent", values.autoIndent, defaults.autoIndent);
        document_.WriteBool("DetectIndentation", values.detectIndentation, defaults.detectIndentation);
        document_.WriteInt("TabWidth", values.globalTabs.tabWidth, defaults.globalTabs.tabWidth);
        document_.WriteInt("IndentWidth", values.globalTabs.indentWidth, defaults.globalTabs.indentWidth);
        document_.WriteInt("BackspaceUnindents", values.backspaceUnindents, defaults.backspaceUnindents);
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
    const bool saved = document_.Save();
    if (saved && (values.saveSettings || explicitSave)) {
        loadedFonts_ = values.fonts;
        loadedExtensions_ = values.schemeExtensions;
    }
    return saved;
}

bool SettingsStore::SaveUILanguage(int language) {
    // The language command reports failure itself. Suppress deferred wxLogGui
    // errors from file operations so they cannot add a second modal dialog.
    const wxLogNull noGuiLog;
    if (wxFileExists(document_.Path()) && !wxIsWritable(document_.Path())) return false;
    if (!document_.Load()) return false;
    const auto normalized = UILanguage::Normalize(language);
    document_.WriteInt("UILanguage", normalized, -1, "Settings2");
    if (!document_.Save()) return false;
    values.uiLanguage = normalized;
    return true;
}

int SettingsStore::MessageSuppression(const wxString& key) {
    if (!document_.Load()) return 0;
    return document_.ReadInt(key, 0, "Suppressed Messages");
}

bool SettingsStore::SuppressMessage(const wxString& key) {
    if (!document_.Load()) return false;
    document_.WriteInt(key, 1, 0, "Suppressed Messages");
    return document_.Save();
}

void SettingsStore::WriteSchemeTabs(size_t index) {
    const auto language = static_cast<SyntaxLanguage>(index);
    if (SyntaxSettingsLanguage(language) != language) return;
    const auto& tabs = values.schemeTabs[index];
    const auto fallback = OriginalSettings{}.schemeTabs[index];
    const auto write = [this, index](const wxString& key, int value, int defaultValue) {
        document_.WriteInt(key, value, defaultValue, SyntaxSettingsSection(static_cast<SyntaxLanguage>(index)));
    };
    write("TabWidth", tabs.tabWidth, fallback.tabWidth);
    write("IndentWidth", tabs.indentWidth, fallback.indentWidth);
    write("TabsAsSpaces", tabs.tabsAsSpaces, fallback.tabsAsSpaces);
    write("UseGlobalTabSettings", tabs.useGlobal, fallback.useGlobal);
}

bool SettingsStore::SaveSchemeTabs(SyntaxLanguage language) {
    if (wxFileExists(document_.Path()) && !wxIsWritable(document_.Path())) return false;
    if (!document_.Load()) return false;
    WriteSchemeTabs(static_cast<size_t>(std::to_underlying(SyntaxSettingsLanguage(language))));
    return document_.Save();
}
