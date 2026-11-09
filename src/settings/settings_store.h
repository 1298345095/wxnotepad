#pragma once

#include "settings/original_settings.h"
#include "syntax/syntax_catalog.h"

class SettingsStore final {
public:
    SettingsStore();
    OriginalSettings values;
    const wxString& Path() const { return path_; }
    bool Save(bool explicitSave);
    bool SaveSchemeTabs(SyntaxLanguage language);
    int MessageSuppression(const wxString& key);
    bool SuppressMessage(const wxString& key);
    void AddRecentFile(const wxString& path);
    void ClearRecentFiles(const wxString& currentPath);
    void RemoveRecentFile(size_t index);

private:
    wxString path_;
    std::vector<wxString> lines_;
    int recentFileCapacity_ = OriginalSettingLimits::MinRecentFiles;
    bool LoadLines();
    bool ReadBool(const wxString& key, bool fallback, const wxString& sectionName = "Settings") const;
    int ReadInt(const wxString& key, int fallback, const wxString& sectionName = "Settings") const;
    wxString ReadDateTimeFormat(const wxString& key) const;
    std::vector<wxString> ReadHistory(const wxString& sectionName, size_t capacity,
                                      bool preserveWhitespace = false) const;
    std::vector<wxString> ReadRecentFiles() const;
    void WriteRecentFiles(const std::vector<wxString>& history);
    void WriteHistory(const wxString& sectionName, const std::vector<wxString>& history);
    void WriteBool(const wxString& key, bool value, bool fallback, const wxString& sectionName = "Settings");
    void WriteInt(const wxString& key, int value, int fallback, const wxString& sectionName = "Settings");
    void WriteSchemeTabs(size_t index);
    bool SaveLines();
};
