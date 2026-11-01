#pragma once

#include <wx/string.h>
#include <vector>

// Defaults and INI names follow Notepad4 89b8b36, Notepad4.cpp::LoadSettings.
struct OriginalSettings {
    bool saveSettings = true;
    bool wordWrap = true;
    bool showLineNumbers = true;
    bool viewWhiteSpace = false;
    int zoomLevel = 100;
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
    bool ReadBool(const wxString& key, bool fallback) const;
    int ReadInt(const wxString& key, int fallback) const;
    void WriteBool(const wxString& key, bool value, bool fallback);
    void WriteInt(const wxString& key, int value, int fallback);
};
