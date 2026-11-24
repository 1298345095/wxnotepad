#pragma once

#include "settings/original_settings.h"
#include "syntax/syntax_catalog.h"
#include <optional>

class SettingsStore final {
public:
    SettingsStore();
    explicit SettingsStore(const wxString& path);
    OriginalSettings values;
    const wxString& Path() const { return path_; }
    bool Save(bool explicitSave);
    bool SwitchStyleTheme(StyleTheme theme);
    bool ImportStyles(const wxString& path);
    bool ExportStyles(const wxString& path);
    FontSettings ResetStyles();
    wxString DarkProfilePath() const;
    bool SaveUILanguage(int language);
    int OriginalRunCommandWidth(int screenWidth, int screenHeight) const;
    bool SaveSchemeTabs(SyntaxLanguage language);
    int MessageSuppression(const wxString& key);
    bool SuppressMessage(const wxString& key);
    void AddRecentFile(const wxString& path);
    void ClearRecentFiles(const wxString& currentPath);
    void RemoveRecentFile(size_t index);

private:
    wxString path_;
    std::vector<wxString> lines_;
    FontSettings loadedFonts_;
    SyntaxExtensions loadedExtensions_;
    void WriteExtensions();
    void ReadCustomColours(FontSettings& profile) const;
    FontSettings LoadStyleProfile(StyleTheme theme);
    void WriteStyleProfile();
    bool SaveStyleProfile();
    wxString ReadStyle(const char* key, const char* fallback, const char* section) const;
    int recentFileCapacity_ = OriginalSettingLimits::MinRecentFiles;
    bool LoadLines();
    enum class KeyCase { Exact, Insensitive };
    std::optional<wxString> FindValue(const wxString& key, const wxString& sectionName,
                                      KeyCase keyCase = KeyCase::Exact) const;
    bool ReadBool(const wxString& key, bool fallback, const wxString& sectionName = "Settings") const;
    int ReadInt(const wxString& key, int fallback, const wxString& sectionName = "Settings") const;
    wxString ReadString(const wxString& key, const wxString& fallback, const wxString& sectionName = "Settings") const;
    void WriteString(const wxString& key, const wxString& value, const wxString& fallback,
                     const wxString& sectionName = "Settings", bool includeDefault = false);
    wxString ReadDateTimeFormat(const wxString& key) const;
    std::vector<wxString> ReadHistory(const wxString& sectionName, size_t capacity,
                                      bool preserveWhitespace = false) const;
    std::vector<wxString> ReadRecentFiles() const;
    void WriteRecentFiles(const std::vector<wxString>& history);
    void WriteHistory(const wxString& sectionName, const std::vector<wxString>& history);
    void ReplaceHistorySection(const wxString& sectionName, const std::vector<wxString>& history);
    void WriteBool(const wxString& key, bool value, bool fallback, const wxString& sectionName = "Settings");
    void WriteInt(const wxString& key, int value, int fallback, const wxString& sectionName = "Settings");
    void WriteSchemeTabs(size_t index);
    bool SaveLines();
};
