#pragma once

#include <wx/string.h>
#include "document/encoding/encoding_detection.h"
#include <vector>
#include <array>
#include "syntax/syntax_catalog.h"

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
    bool viewWhiteSpace = false;
    bool autoSelect = true;
    bool noCgiGuess = false;
    bool fixLineEndings = false;
    bool fixTrailingBlanks = false;
    EncodingOptions encoding;
    int zoomLevel = 100;
    int selectOption = 15;
    bool saveFindReplace = false;
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
};

class SettingsStore final {
public:
    SettingsStore();
    OriginalSettings values;
    const wxString& Path() const { return path_; }
    bool Save(bool explicitSave);
    bool SaveSchemeTabs(SyntaxLanguage language);
    int MessageSuppression(const wxString& key);
    bool SuppressMessage(const wxString& key);

private:
    wxString path_;
    std::vector<wxString> lines_;
    bool LoadLines();
    bool ReadBool(const wxString& key, bool fallback, const wxString& sectionName = "Settings") const;
    int ReadInt(const wxString& key, int fallback, const wxString& sectionName = "Settings") const;
    std::vector<wxString> ReadHistory(const wxString& sectionName) const;
    void WriteHistory(const wxString& sectionName, const std::vector<wxString>& history);
    void WriteBool(const wxString& key, bool value, bool fallback, const wxString& sectionName = "Settings");
    void WriteInt(const wxString& key, int value, int fallback, const wxString& sectionName = "Settings");
    void WriteSchemeTabs(size_t index);
    bool SaveLines();
};
