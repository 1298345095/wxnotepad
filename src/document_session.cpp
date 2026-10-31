#include "document_session.h"
#include "editor_control.h"
#include "ScintillaTypes.h"
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/utils.h>
#include <cstdio>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
bool IsUtf8(std::string_view bytes) {
    for (size_t i = 0; i < bytes.size();) {
        const auto lead = static_cast<unsigned char>(bytes[i++]);
        if (lead < 0x80) continue;
        unsigned int value;
        size_t remaining;
        unsigned int minimum;
        if (lead >= 0xc2 && lead <= 0xdf) { value = lead & 0x1f; remaining = 1; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { value = lead & 0x0f; remaining = 2; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { value = lead & 7; remaining = 3; minimum = 0x10000; }
        else return false;
        if (remaining > bytes.size() - i) return false;
        while (remaining--) {
            const auto next = static_cast<unsigned char>(bytes[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

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
    return SaveTo(*destination);
}

SaveResult DocumentSession::Save() {
    if (path_.empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(path_);
}

SaveResult DocumentSession::SaveTo(const wxString& destination) {
    const auto length = static_cast<size_t>(editor_.Send(Scintilla::Message::GetLength));
    std::string bytes(length + 1, '\0');
    editor_.Send(Scintilla::Message::GetText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    bytes.resize(length);
    if (hasUtf8Bom_) bytes.insert(0, "\xef\xbb\xbf");
    const auto error = WriteSafely(destination, bytes);
    if (!error.empty()) return {SaveStatus::Failed, error};
    path_ = destination;
    editor_.Send(Scintilla::Message::SetSavePoint);
    return {SaveStatus::Saved, {}};
}

OpenResult DocumentSession::Open(const std::optional<wxString>& source, const std::function<bool()>& beforeReplace) {
    if (!source) return {OpenStatus::Cancelled, {}};
    wxLogNull quiet;
    wxFile file(*source, wxFile::read);
    if (!file.IsOpened()) return {OpenStatus::Failed, "Could not open the file for reading."};
    const auto length = file.Length();
    if (length < 0) return {OpenStatus::Failed, "Could not determine the file size."};
    std::string bytes(static_cast<size_t>(length), '\0');
    if (file.Read(bytes.data(), bytes.size()) != length || !file.Close())
        return {OpenStatus::Failed, "Could not read the complete file."};
    const bool bom = bytes.starts_with("\xef\xbb\xbf");
    if (bom) bytes.erase(0, 3);
    if (!IsUtf8(bytes)) return {OpenStatus::Failed, "The file is not valid UTF-8. No other encoding was assumed."};
    if (!beforeReplace()) return {OpenStatus::Cancelled, {}};
    Replace(bytes, *source, bom);
    return {OpenStatus::Opened, {}};
}

void DocumentSession::Replace(const std::string& bytes, const wxString& path, bool bom) {
    editor_.Send(Scintilla::Message::SetUndoCollection, 0);
    editor_.Send(Scintilla::Message::ClearAll);
    editor_.Send(Scintilla::Message::AddText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    editor_.Send(Scintilla::Message::SetUndoCollection, 1);
    editor_.Send(Scintilla::Message::EmptyUndoBuffer);
    editor_.Send(Scintilla::Message::GotoPos, 0);
    editor_.Send(Scintilla::Message::SetXOffset, 0);
    editor_.Send(Scintilla::Message::SetSavePoint);
    path_ = path;
    hasUtf8Bom_ = bom;
}

void DocumentSession::New() {
    Replace({}, {}, false);
    editor_.Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::CrLf));
}
