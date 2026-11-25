#pragma once

#include "settings/original_settings.h"
#include "settings/ini_document.h"
#include "syntax/syntax_catalog.h"

class SettingsStore final {
public:
    SettingsStore();
    explicit SettingsStore(const wxString& path);
    OriginalSettings values;
    const wxString& Path() const { return document_.Path(); }
    bool Save(bool explicitSave);
    bool SwitchStyleTheme(StyleTheme theme);
    bool ImportStyles(const wxString& path);
    bool ExportStyles(const wxString& path);
    FontSettings ResetStyles();
    wxString DarkProfilePath() const;
    bool SaveUILanguage(int language);
    int OriginalRunCommandWidth(int screenWidth, int screenHeight) const;
    wxString SelectionActionTemplate(SelectionAction action);
    bool SaveSchemeTabs(SyntaxLanguage language);
    int MessageSuppression(const wxString& key);
    bool SuppressMessage(const wxString& key);
    void AddRecentFile(const wxString& path);
    void ClearRecentFiles(const wxString& currentPath);
    void RemoveRecentFile(size_t index);

private:
    IniDocument document_;
    FontSettings loadedFonts_;
    SyntaxExtensions loadedExtensions_;
    void WriteExtensions();
    static void ReadCustomColours(const IniDocument& document, FontSettings& profile);
    FontSettings LoadStyleProfile(StyleTheme theme);
    void WriteStyleProfile(IniDocument& document);
    bool SaveStyleProfile();
    static wxString ReadStyle(const IniDocument& document, const char* key, const char* fallback, const char* section);
    int recentFileCapacity_ = OriginalSettingLimits::MinRecentFiles;
    wxString ReadDateTimeFormat(const wxString& key) const;
    std::vector<wxString> ReadRecentFiles() const;
    void WriteRecentFiles(const std::vector<wxString>& history);
    void WriteHistory(const wxString& sectionName, const std::vector<wxString>& history);
    void WriteSchemeTabs(size_t index);
};
