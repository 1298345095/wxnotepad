#pragma once

#include <wx/string.h>
#include <vector>

// Defaults and INI names follow Notepad4 89b8b36, Notepad4.cpp::LoadSettings.
struct OriginalSettings {
    bool saveSettings = true;
    bool wordWrap = true;
    bool showLineNumbers = true;
    bool viewWhiteSpace = false;
    bool autoSelect = true;
    bool noCgiGuess = false;
    int zoomLevel = 100;
    int selectOption = 15;
    bool saveFindReplace = false;
    int findReplaceOption = 2;
    int findReplaceFlag = 0;
    std::vector<wxString> recentFind;
    std::vector<wxString> recentReplace;
};

class SettingsStore final {
public:
    SettingsStore();
    OriginalSettings values;
    const wxString& Path() const { return path_; }
    bool Save(bool explicitSave);

private:
    wxString path_;
    std::vector<wxString> lines_;
    void LoadLines();
    bool ReadBool(const wxString& key, bool fallback, const wxString& sectionName = "Settings") const;
    int ReadInt(const wxString& key, int fallback) const;
    std::vector<wxString> ReadHistory(const wxString& sectionName) const;
    void WriteHistory(const wxString& sectionName, const std::vector<wxString>& history);
    void WriteBool(const wxString& key, bool value, bool fallback, const wxString& sectionName = "Settings");
    void WriteInt(const wxString& key, int value, int fallback, const wxString& sectionName = "Settings");
};
