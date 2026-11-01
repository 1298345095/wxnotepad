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
    EncodingOptions encoding;
    int zoomLevel = 100;
    int lineSelection = 1;
    OriginalTabs globalTabs;
    std::array<OriginalTabs, 4> schemeTabs{{{}, {}, {}, {2, 2, true, false}}};
    bool tabIndents = true;
    int backspaceUnindents = 2;
};

class SettingsStore final {
public:
    SettingsStore();
    OriginalSettings values;
    const wxString& Path() const { return path_; }
    bool Save(bool explicitSave);
    bool SaveSchemeTabs(SyntaxLanguage language);

private:
    wxString path_;
    std::vector<wxString> lines_;
    void LoadLines();
    bool ReadBool(const wxString& key, bool fallback, const wxString& sectionName = "Settings") const;
    int ReadInt(const wxString& key, int fallback, const wxString& sectionName = "Settings") const;
    void WriteBool(const wxString& key, bool value, bool fallback, const wxString& sectionName = "Settings");
    void WriteInt(const wxString& key, int value, int fallback, const wxString& sectionName = "Settings");
    void WriteSchemeTabs(size_t index);
    bool SaveLines();
};
