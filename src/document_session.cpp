#include "document_session.h"
#include "editor_control.h"
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/utils.h>
#include <cstdio>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
struct PendingSave {
    wxString path;
    wxFile file;
    ~PendingSave() {
        file.Close();
        if (!path.empty()) wxRemoveFile(path);
    }
};

wxString WriteSafely(const wxString& destination, const std::string& bytes) {
    // Errors are returned to the application instead of opening wx log dialogs.
    wxLogNull quiet;
    PendingSave temporary;
    temporary.path = wxFileName::CreateTempFileName(destination + ".wxnotepad-", &temporary.file);
    if (temporary.path.empty()) return "Could not create a temporary file beside the destination. Check the folder and access permissions.";
    if (temporary.file.Write(bytes.data(), bytes.size()) != bytes.size() || !temporary.file.Flush())
        return "Could not write the complete file. Check free space and access permissions.";
    if (!temporary.file.Close()) return "Could not close the completed temporary file.";
#ifdef __WXMSW__
    const bool replaced = MoveFileExW(temporary.path.wc_str(), destination.wc_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    const bool replaced = ::rename(temporary.path.utf8_str(), destination.utf8_str()) == 0;
#endif
    if (!replaced) return "Could not replace the destination: " + wxString(wxSysErrorMsg());
    temporary.path.clear();
    return {};
}
}

SaveResult DocumentSession::SaveFirst(const std::optional<wxString>& destination) {
    if (!destination) return {SaveStatus::Cancelled, {}};
    if (!path_.empty()) return {SaveStatus::Failed, "This document already has a file name."};
    if (destination->empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    const auto length = static_cast<size_t>(editor_.Send(Scintilla::Message::GetLength));
    std::string bytes(length + 1, '\0');
    editor_.Send(Scintilla::Message::GetText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    bytes.resize(length);
    const auto error = WriteSafely(*destination, bytes);
    if (!error.empty()) return {SaveStatus::Failed, error};
    path_ = *destination;
    editor_.Send(Scintilla::Message::SetSavePoint);
    return {SaveStatus::Saved, {}};
}
