#include "document_session.h"
#include "editor_control.h"
#include "gbk_codec.h"
#include "utf16_codec.h"
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

SaveResult DocumentSession::SaveFirst(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss) {
    if (!destination) return {SaveStatus::Cancelled, {}};
    if (!path_.empty()) return {SaveStatus::Failed, "This document already has a file name."};
    if (destination->empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(*destination, decideLoss);
}

SaveResult DocumentSession::Save(const GbkLossDecision& decideLoss) {
    if (path_.empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(path_, decideLoss);
}

SaveResult DocumentSession::SaveAs(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss) {
    if (!destination) return {SaveStatus::Cancelled, {}};
    if (destination->empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(*destination, decideLoss);
}

SaveResult DocumentSession::SaveTo(const wxString& destination, const GbkLossDecision& decideLoss) {
    const auto length = static_cast<size_t>(editor_.Send(Scintilla::Message::GetLength));
    std::string bytes(length + 1, '\0');
    editor_.Send(Scintilla::Message::GetText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    bytes.resize(length);
    std::optional<std::string> savedText;
    auto savedEncoding = encoding_;
    bool savedBom = hasBom_;
    if (encoding_ == FileEncoding::Gbk) {
        if (!IsUtf8(bytes)) return {SaveStatus::Failed, "The document contains invalid UTF-8."};
        const auto encoded = EncodeGbk(bytes);
        if (encoded.loss) {
            const auto choice = decideLoss ? decideLoss() : GbkLossChoice::Cancel;
            if (choice == GbkLossChoice::Cancel) return {SaveStatus::Cancelled, {}};
            if (choice == GbkLossChoice::Utf8) { savedEncoding = FileEncoding::Utf8; savedBom = false; }
            else { bytes = encoded.bytes; savedText = DecodeGbk(bytes); }
        } else bytes = encoded.bytes;
    } else if (encoding_ == FileEncoding::Utf16Le) {
        auto encoded = EncodeUtf16Le(bytes);
        if (!encoded) return {SaveStatus::Failed, "The document could not be encoded as UTF-16LE."};
        bytes = std::move(*encoded);
        if (hasBom_) bytes.insert(0, "\xff\xfe");
    } else if (hasBom_) bytes.insert(0, "\xef\xbb\xbf");
    const auto error = WriteSafely(destination, bytes);
    if (!error.empty()) return {SaveStatus::Failed, error};
    if (savedText) {
        editor_.Send(Scintilla::Message::BeginUndoAction);
        editor_.Send(Scintilla::Message::SetTargetStart, 0);
        editor_.Send(Scintilla::Message::SetTargetEnd, length);
        editor_.Send(Scintilla::Message::ReplaceTarget, savedText->size(), reinterpret_cast<intptr_t>(savedText->data()));
        editor_.Send(Scintilla::Message::EndUndoAction);
    }
    path_ = destination;
    encoding_ = savedEncoding;
    hasBom_ = savedBom;
    editor_.Send(Scintilla::Message::SetSavePoint);
    return {SaveStatus::Saved, {}};
}

OpenResult DocumentSession::Open(const std::optional<wxString>& source, const std::function<bool()>& beforeReplace,
                                 std::optional<FileEncoding> encoding) {
    if (!source) return {OpenStatus::Cancelled, {}};
    wxLogNull quiet;
    if (!wxFileName::FileExists(*source))
        return {OpenStatus::Failed, "Choose an existing file to open."};
    wxFile file(*source, wxFile::read);
    if (!file.IsOpened()) return {OpenStatus::Failed, "Could not open the file for reading."};
    const auto length = file.Length();
    if (length < 0) return {OpenStatus::Failed, "Could not determine the file size."};
    std::string bytes(static_cast<size_t>(length), '\0');
    if (file.Read(bytes.data(), bytes.size()) != length || !file.Close())
        return {OpenStatus::Failed, "Could not read the complete file."};
    const auto format = encoding.value_or(bytes.starts_with("\xff\xfe") ? FileEncoding::Utf16Le : FileEncoding::Utf8);
    const bool bom = format == FileEncoding::Utf16Le ? bytes.starts_with("\xff\xfe") :
                     format == FileEncoding::Utf8 && bytes.starts_with("\xef\xbb\xbf");
    if (bom) bytes.erase(0, format == FileEncoding::Utf16Le ? 2 : 3);
    if (format == FileEncoding::Gbk) {
        const auto decoded = DecodeGbk(bytes);
        if (!decoded) return {OpenStatus::Failed, "The file is not valid GBK (CP936). The document was not changed."};
        bytes = *decoded;
    } else if (format == FileEncoding::Utf16Le) {
        auto decoded = DecodeUtf16Le(bytes);
        if (!decoded) return {OpenStatus::Failed, "The file is not valid UTF-16LE."};
        bytes = std::move(*decoded);
    } else if (!IsUtf8(bytes)) return {OpenStatus::Failed, "The file is not valid UTF-8. No other encoding was assumed."};
    const bool wasModified = editor_.Send(Scintilla::Message::GetModify);
    if (!beforeReplace()) return {OpenStatus::Cancelled, {}};
    // Save may have rewritten the selected source (including a newly chosen
    // first-save destination). Read it again after that explicit Save succeeds.
    if (wasModified && !editor_.Send(Scintilla::Message::GetModify))
        return Open(source, [] { return true; }, encoding);
    Replace(bytes, *source, format, bom);
    return {OpenStatus::Opened, {}};
}

void DocumentSession::Replace(const std::string& bytes, const wxString& path, FileEncoding encoding, bool bom) {
    editor_.Send(Scintilla::Message::SetUndoCollection, 0);
    editor_.Send(Scintilla::Message::ClearAll);
    editor_.Send(Scintilla::Message::AddText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    editor_.Send(Scintilla::Message::SetUndoCollection, 1);
    editor_.Send(Scintilla::Message::EmptyUndoBuffer);
    editor_.Send(Scintilla::Message::GotoPos, 0);
    editor_.Send(Scintilla::Message::SetXOffset, 0);
    editor_.Send(Scintilla::Message::SetSavePoint);
    path_ = path;
    encoding_ = encoding;
    hasBom_ = bom;
}

void DocumentSession::New() {
    Replace({}, {}, FileEncoding::Utf8, false);
    editor_.Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::CrLf));
}
