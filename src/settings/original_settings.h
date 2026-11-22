#pragma once

#include "settings/original_settings_constants.h"
#include "editor/font_settings.h"
#include "editor/color_calltip_mode.h"
#include <utility>
#include "settings/ui_language.h"
#include "editor/display_units.h"
#include "document/encoding/encoding_detection.h"
#include "editor/caret_appearance.h"
#include "editor/word_completion.h"
#include "settings/file_watching_options.h"
#include "settings/print_options.h"
#include "settings/auto_save_options.h"
#include "syntax/syntax_catalog.h"
#include <ScintillaTypes.h>
#include <wx/string.h>
#include <array>
#include <vector>

struct OriginalTabs {
    int tabWidth = 4;
    int indentWidth = 4;
    bool tabsAsSpaces = false;
    bool useGlobal = true;
};

enum class TitlePathNameFormat { NameOnly = 0, NameFirst = 1, FullPath = 2 };

// Defaults and INI names follow Notepad4 89b8b36, Notepad4.cpp::LoadSettings.
struct OriginalSettings {
    FontSettings fonts;
    bool saveSettings = true;
    bool saveBeforeRunningTools = false;
    wxString openWithDirectory;
    int runCommandWidthDIP = 0;
    uint16_t uiLanguage = UILanguage::System;
    bool showMenu = true;
    bool showToolbar = true;
    bool showStatusbar = true;
    int fullScreenMode = FullScreenMode_Default;
    EscapeFunction escapeFunction = EscapeFunction::None;
    TitlePathNameFormat pathNameFormat = TitlePathNameFormat::NameFirst;
    bool wordWrap = true;
    int wordWrapMode = std::to_underlying(Scintilla::Wrap::Auto);
    WrapContinuationIndent wordWrapIndent = WrapContinuationIndent::None;
    bool wordWrapSelectSubLine = false;
    int endAtLastLine = 1;
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
    PrintOptions printing;
    FileWatchingOptions fileWatching;
    AutoSaveOptions autoSave;
    EncodingOptions encoding;
    int zoomLevel = DisplayUnits::ZoomPercentScale;
    int renderingTechnology = std::to_underlying(Scintilla::Technology::DirectWrite);
    CaretAppearance caret;
    int highlightCurrentLine = 12;
    int longLinesLimit = 80;
    bool markLongLines = false;
    int longLineMode = 1;
    int selectOption = SelectOption_Default;
    int autoInsertMask = AutoInsertMask_Default;
    WordCompletionSettings completion;
    int markOccurrences = MarkOccurrenceOption_Default;
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
    std::array<OriginalTabs, static_cast<size_t>(std::to_underlying(SyntaxLanguage::Count))> schemeTabs = [] {
        std::array<OriginalTabs, static_cast<size_t>(std::to_underlying(SyntaxLanguage::Count))> tabs{};
        for (const auto& scheme : SyntaxCatalog()) {
            const auto& original = scheme.original;
            tabs[static_cast<size_t>(std::to_underlying(scheme.language))] = {
                original.tabWidth, original.indentWidth,
                (original.lexerAttributes & SyntaxLexerAttribute::TabsAsSpaces) != 0,
                (original.lexerAttributes & SyntaxLexerAttribute::NoGlobalTabSettings) == 0};
        }
        return tabs;
    }();
    bool tabIndents = true;
    bool autoIndent = true;
    bool detectIndentation = true;
    int backspaceUnindents = 2;
    wxString dateTimeShort;
    wxString dateTimeLong;
    bool viewEOLs = false;
    bool showUnicodeControlCharacter = false;
    ColorCallTipMode showCallTip = ColorCallTipMode::Off;
    bool showWordWrapSymbols = false;
    int wordWrapSymbols = 2;
    // wxnotepad opt-in; false retains original growth-only width tracking.
    bool autoShrinkScrollWidth = false;
};
