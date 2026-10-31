#pragma once

#include <wx/string.h>
#include <optional>

class EditorControl;

enum class SaveStatus { Saved, Cancelled, Failed };
struct SaveResult {
    SaveStatus status;
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

private:
    EditorControl& editor_;
    wxString path_;
    SaveResult SaveTo(const wxString& destination);
};
