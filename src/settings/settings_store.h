#pragma once

#include <wx/string.h>
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
    bool ReadBool(const wxString& key, bool fallback) const;
    void WriteBool(const wxString& key, bool value, bool fallback);
    wxString ReadValue(const wxString& section, const wxString& key) const;
    int ReadInt(const wxString& section, const wxString& key, int fallback) const;
    void WriteValue(const wxString& section, const wxString& key, const wxString& value, bool omit);
    void WriteSchemeTabs(size_t index);
    bool SaveLines();
};
