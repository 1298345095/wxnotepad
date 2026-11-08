#pragma once

#include "settings/original_settings_constants.h"
#include "editor/display_units.h"
#include "document/encoding/encoding_detection.h"
#include "editor/caret_appearance.h"
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
    bool fixLineEndings = false;
    bool fixTrailingBlanks = false;
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
    std::array<OriginalTabs, 4> schemeTabs{{{}, {}, {}, {2, 2, true, false}}};
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
