#pragma once

#include <wx/string.h>
#include <cstddef>
#include <optional>
#include <vector>

// One file's original INI representation. Reads and updates preserve unrelated
// lines, while saving retains the established UTF-16LE/BOM/CRLF format.
class IniDocument final {
public:
    explicit IniDocument(const wxString& path) : path_(path) {}
    const wxString& Path() const { return path_; }
    // A missing file starts with defaultText; a read failure leaves no lines.
    bool Load(const wxString& defaultText = {});
    bool Save() const;
    std::vector<wxString> SectionNames() const;
    enum class KeyCase { Exact, Insensitive };
    std::optional<wxString> FindValue(const wxString& key, const wxString& sectionName,
                                      KeyCase keyCase = KeyCase::Exact) const;
    bool ReadBool(const wxString& key, bool fallback, const wxString& sectionName = "Settings") const;
    int ReadInt(const wxString& key, int fallback, const wxString& sectionName = "Settings") const;
    wxString ReadString(const wxString& key, const wxString& fallback, const wxString& sectionName = "Settings") const;
    // Writes replace matching keys in every matching section. A default value
    // removes the stored field unless includeDefault is set; other values,
    // including an explicit empty string, are retained.
    void WriteBool(const wxString& key, bool value, bool defaultValue, const wxString& sectionName = "Settings");
    void WriteInt(const wxString& key, int value, int defaultValue, const wxString& sectionName = "Settings");
    void WriteString(const wxString& key, const wxString& value, const wxString& defaultValue,
                     const wxString& sectionName = "Settings", bool includeDefault = false);
    std::vector<wxString> ReadHistory(const wxString& sectionName, size_t capacity,
                                      bool preserveWhitespace = false) const;
    void ReplaceHistorySection(const wxString& sectionName, const std::vector<wxString>& history);

private:
    wxString path_;
    std::vector<wxString> lines_;
};
