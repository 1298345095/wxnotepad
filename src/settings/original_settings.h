#pragma once

#include "settings/original_settings_constants.h"
#include "editor/display_units.h"
#include "document/encoding/encoding_detection.h"
#include "editor/caret_appearance.h"
#include "settings/file_watching_options.h"
#include "syntax/syntax_catalog.h"
#include <wx/string.h>
#include <array>
#include <vector>

struct OriginalTabs {
    int tabWidth = 4;
    int indentWidth = 4;
    bool tabsAsSpaces = false;
    bool useGlobal = true;
};

// Defaults and INI names follow Notepad4 89b8b36, Notepad4.cpp::LoadSettings.
struct OriginalSettings {
    bool saveSettings = true;
    bool wordWrap = true;
    bool showLineNumbers = true;
    bool showBookmarkMargin = false;
    int changeHistoryMarker = 0;
    bool matchBraces = true;
    bool showCodeFolding = true;
    bool highlightCurrentBlock = true;
    bool viewWhiteSpace = false;
    bool showIndentGuides = false;
    bool simpleIndentGuides = false;
    bool autoSelect = true;
    bool noCgiGuess = false;
    bool noHtmlGuess = false;
    bool fixLineEndings = false;
    bool fixTrailingBlanks = false;
    FileWatchingOptions fileWatching;
    EncodingOptions encoding;
    int zoomLevel = DisplayUnits::ZoomPercentScale;
    CaretAppearance caret;
    int highlightCurrentLine = 12;
    int longLinesLimit = 80;
    bool markLongLines = false;
    int longLineMode = 1;
    int selectOption = SelectOption_Default;
    int autoInsertMask = AutoInsertMask_Default;
    int markOccurrences = 1;
    bool saveFindReplace = false;
    bool saveRecentFiles = false;
    int maxRecentFiles = OriginalSettingLimits::MinRecentFiles;
    bool relativeFileMRU = true;
    bool portableMyDocs = true;
    std::vector<wxString> recentFiles;
    int findReplaceOption = 2;
    int findReplaceFlag = 0;
    std::vector<wxString> recentFind;
    std::vector<wxString> recentReplace;
    int lineSelection = 1;
    int defaultEOLMode = 0;
    bool warnLineEndings = true;
    OriginalTabs globalTabs;
    std::array<OriginalTabs, static_cast<size_t>(SyntaxLanguage::Count)> schemeTabs = [] {
        std::array<OriginalTabs, static_cast<size_t>(SyntaxLanguage::Count)> tabs{};
        // GCC 13 omits trailing default member initializers in this returned
        // aggregate; fill explicitly so XML keeps 4/4 and global inheritance.
        tabs.fill(OriginalTabs{});
        tabs[static_cast<size_t>(SyntaxLanguage::Json)] = {2, 2, true, false};
        tabs[static_cast<size_t>(SyntaxLanguage::Html)] = {2, 2, true, false};
        return tabs;
    }();
    bool tabIndents = true;
    bool autoIndent = true;
    bool detectIndentation = true;
    int backspaceUnindents = 2;
    wxString dateTimeShort;
    wxString dateTimeLong;
    bool viewEOLs = false;
    bool showWordWrapSymbols = false;
    int wordWrapSymbols = 2;
    // wxnotepad opt-in; false retains original growth-only width tracking.
    bool autoShrinkScrollWidth = false;
};
