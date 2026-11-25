#include "settings/ini_document.h"
#include <wx/convauto.h>
#include <wx/ffile.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/tokenzr.h>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>

bool IniDocument::Load(const wxString& defaultText) {
    lines_.clear();
    wxString text = defaultText;
    if (wxFileExists(path_)) {
        wxFFile input(path_, "rb");
        if (!input.IsOpened()) return false;
        const auto length = input.Length();
        if (length < 0) return false;
        std::string bytes(static_cast<size_t>(length), '\0');
        if (input.Read(bytes.data(), bytes.size()) != bytes.size() || input.Error() || !input.Close()) return false;
#ifdef __WXMSW__
        if (wxConvAuto::DetectBOM(bytes.data(), bytes.size()) == wxBOM_None)
            text = wxString(bytes.data(), wxConvLocal, bytes.size());
        else
#endif
            text = wxString(bytes.data(), wxConvAuto(wxFONTENCODING_SYSTEM), bytes.size());
    }
    wxStringTokenizer lines(text, "\n", wxTOKEN_RET_EMPTY_ALL);
    while (lines.HasMoreTokens()) {
        wxString line = lines.GetNextToken();
        if (line.EndsWith("\r")) line.RemoveLast();
        lines_.push_back(line);
    }
    if (!lines_.empty() && lines_.back().empty()) lines_.pop_back();
    return true;
}

std::optional<wxString> IniDocument::FindValue(const wxString& key, const wxString& sectionName,
                                               KeyCase keyCase) const {
    bool section = false;
    for (auto line : lines_) {
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            section = line.CmpNoCase("[" + sectionName + "]") == 0;
        } else if (section && line.Contains('=')) {
            const auto name = line.BeforeFirst('=').Trim().Trim(false);
            if (keyCase == KeyCase::Exact ? name == key : name.CmpNoCase(key) == 0) return line.AfterFirst('=');
        }
    }
    return std::nullopt;
}

bool IniDocument::ReadBool(const wxString& key, bool fallback, const wxString& sectionName) const {
    if (auto value = FindValue(key, sectionName)) {
        value->Trim().Trim(false);
        // Original IniSectionParser::GetBoolImpl accepts a leading 0 or 1.
        if (value->StartsWith("0")) return false;
        if (value->StartsWith("1")) return true;
    }
    return fallback;
}

int IniDocument::ReadInt(const wxString& key, int fallback, const wxString& sectionName) const {
    const auto field = FindValue(key, sectionName);
    if (!field) return fallback;
    // Original CRTStrToInt accepts a decimal prefix (including a sign).
    const auto value = field->ToStdString(wxConvUTF8);
    char* end = nullptr;
    const long number = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) return fallback;
    return static_cast<int>(std::clamp(number, static_cast<long>(std::numeric_limits<int>::min()),
                                       static_cast<long>(std::numeric_limits<int>::max())));
}

void IniDocument::WriteBool(const wxString& key, bool value, bool defaultValue, const wxString& sectionName) {
    WriteInt(key, value, defaultValue, sectionName);
}

std::vector<wxString> IniDocument::ReadHistory(const wxString& sectionName, size_t capacity,
                                               bool preserveWhitespace) const {
    std::vector<wxString> history;
    bool section = false;
    for (const auto& raw : lines_) {
        wxString line = raw;
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            section = line.CmpNoCase("[" + sectionName + "]") == 0;
        } else if (section && !line.StartsWith(";") && !line.StartsWith("#") && line.Contains('=')) {
            wxString value = raw.AfterFirst('=');
            wxString trimmed = value;
            trimmed.Trim().Trim(false);
            if (trimmed.StartsWith("\"") && trimmed.EndsWith("\"") && trimmed.length() >= 2)
                value = trimmed.Mid(1, trimmed.length() - 2);
            else if (!preserveWhitespace)
                value = trimmed;
            if (!value.empty()) history.push_back(value);
            if (history.size() == capacity) break;
        }
    }
    return history;
}

void IniDocument::ReplaceHistorySection(const wxString& sectionName, const std::vector<wxString>& history) {
    bool section = false;
    for (size_t index = 0; index < lines_.size();) {
        wxString line = lines_[index];
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            section = line.CmpNoCase("[" + sectionName + "]") == 0;
        }
        if (section)
            lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(index));
        else
            ++index;
    }
    if (history.empty()) return;
    lines_.push_back("[" + sectionName + "]");
    for (size_t index = 0; index < history.size(); ++index)
        lines_.push_back(wxString::Format("%02zu=\"", index + 1) + history[index] + "\"");
}

void IniDocument::WriteInt(const wxString& key, int value, int defaultValue, const wxString& sectionName) {
    WriteString(key, wxString::Format("%d", value), wxString::Format("%d", defaultValue), sectionName);
}

void IniDocument::WriteString(const wxString& key, const wxString& value, const wxString& defaultValue,
                              const wxString& sectionName, bool includeDefault) {
    bool settings = false;
    bool foundSection = false;
    size_t insertion = lines_.size();
    for (size_t index = 0; index < lines_.size();) {
        wxString line = lines_[index];
        line.Trim().Trim(false);
        if (line.StartsWith("[")) {
            settings = line.CmpNoCase("[" + sectionName + "]") == 0;
            if (settings && !foundSection) {
                insertion = index + 1;
                foundSection = true;
            }
        } else if (settings && line.Contains('=') && line.BeforeFirst('=').Trim().Trim(false) == key) {
            lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        ++index;
    }
    if (value == defaultValue && !includeDefault) return;
    if (!foundSection) {
        lines_.push_back("[" + sectionName + "]");
        insertion = lines_.size();
    }
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insertion), key + "=" + value);
}

wxString IniDocument::ReadString(const wxString& key, const wxString& fallback, const wxString& sectionName) const {
    auto value = FindValue(key, sectionName);
    if (!value) return fallback;
    value->Trim().Trim(false);
    if (value->StartsWith("\"") && value->EndsWith("\"") && value->length() >= 2)
        value = value->Mid(1, value->length() - 2);
    return value->empty() ? fallback : *value;
}

bool IniDocument::Save() const {
    wxString text(wxUniChar(0xFEFF));
    for (const auto& line : lines_)
        text += line + "\r\n";
    wxTempFile output(path_);
    return output.IsOpened() && output.Write(text, wxMBConvUTF16LE()) && output.Commit();
}

std::vector<wxString> IniDocument::SectionNames() const {
    std::vector<wxString> names;
    for (auto line : lines_) {
        line = line.Strip(wxString::both);
        if (line.StartsWith("[") && line.EndsWith("]")) names.push_back(line.Mid(1, line.length() - 2));
    }
    return names;
}
