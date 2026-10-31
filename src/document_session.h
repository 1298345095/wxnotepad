#pragma once

#include <wx/string.h>
#include <optional>
#include <functional>
#include <string>

class EditorControl;

enum class SaveStatus { Saved, Cancelled, Failed };
struct SaveResult {
    SaveStatus status;
    wxString error;
};
enum class OpenStatus { Opened, Cancelled, Failed };
enum class FileEncoding { Utf8, Utf16Le, Gbk };
struct OpenResult {
    OpenStatus status;
    wxString error;
};

// File identity belongs to the session; the original core owns text and history.
class DocumentSession final {
public:
    explicit DocumentSession(EditorControl& editor) : editor_(editor) {}
    const wxString& Path() const { return path_; }
    // A chosen path represents the caller's approved destination; no path is Cancel.
    SaveResult SaveFirst(const std::optional<wxString>& destination);
    SaveResult Save();
    SaveResult SaveAs(const std::optional<wxString>& destination);
    OpenResult Open(const std::optional<wxString>& source, const std::function<bool()>& beforeReplace,
                    std::optional<FileEncoding> encoding = std::nullopt);
    void New();
    bool HasUtf8Bom() const { return encoding_ == FileEncoding::Utf8 && hasBom_; }
    FileEncoding Encoding() const { return encoding_; }
    bool HasBom() const { return hasBom_; }

private:
    EditorControl& editor_;
    wxString path_;
    FileEncoding encoding_ = FileEncoding::Utf8;
    bool hasBom_ = false;
    SaveResult SaveTo(const wxString& destination);
    void Replace(const std::string& bytes, const wxString& path, FileEncoding encoding, bool bom);
};
