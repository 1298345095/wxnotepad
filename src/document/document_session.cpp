#include "document/document_session.h"
#include "editor/editor_control.h"
#include "syntax/syntax_scheme.h"
#include "document/encoding/gbk_codec.h"
#include "document/encoding/utf16_codec.h"
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
Scintilla::EndOfLine DetectEol(std::string_view bytes) {
    size_t crlf = 0;
    size_t cr = 0;
    size_t lf = 0;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '\r') {
            if (i + 1 < bytes.size() && bytes[i + 1] == '\n') {
                ++crlf;
                ++i;
            } else
                ++cr;
        } else if (bytes[i] == '\n')
            ++lf;
    }
    // Original EditDetectEOLMode policy with the current default CRLF:
    // retain CRLF when tied for most common; otherwise prefer LF over CR.
    if (crlf >= cr && crlf >= lf) return Scintilla::EndOfLine::CrLf;
    return lf >= cr ? Scintilla::EndOfLine::Lf : Scintilla::EndOfLine::Cr;
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
    if (temporary.path.empty())
        return "Could not create a temporary file beside the destination. Check the folder and access permissions.";
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
} // namespace

SaveResult DocumentSession::SaveFirst(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss,
                                      SaveFileType fileType) {
    if (!destination) return {SaveStatus::Cancelled, {}};
    if (!path_.empty()) return {SaveStatus::Failed, "This document already has a file name."};
    if (destination->empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(*destination, decideLoss, fileType);
}

SaveResult DocumentSession::Save(const GbkLossDecision& decideLoss) {
    if (path_.empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(path_, decideLoss);
}

SaveResult DocumentSession::SaveAs(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss,
                                   SaveFileType fileType) {
    if (!destination) return {SaveStatus::Cancelled, {}};
    if (destination->empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(*destination, decideLoss, fileType);
}

SaveResult DocumentSession::SaveTo(const wxString& destination, const GbkLossDecision& decideLoss,
                                   std::optional<SaveFileType> fileType) {
    const auto length = static_cast<size_t>(editor_.Send(Scintilla::Message::GetLength));
    std::string bytes(length + 1, '\0');
    editor_.Send(Scintilla::Message::GetText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    bytes.resize(length);
    if (encoding_ == FileEncoding::Gbk) {
        if (!IsUtf8(bytes)) return {SaveStatus::Failed, "The document contains invalid UTF-8."};
        const auto encoded = EncodeGbk(bytes);
        if (encoded.loss && (!decideLoss || decideLoss() != GbkLossChoice::Replace)) return {SaveStatus::Cancelled, {}};
        bytes = encoded.bytes;
    } else if (encoding_ == FileEncoding::Utf16Le) {
        auto encoded = EncodeUtf16Le(bytes);
        if (!encoded) return {SaveStatus::Failed, "The document could not be encoded as UTF-16LE."};
        bytes = std::move(*encoded);
        if (hasBom_) bytes.insert(0, "\xff\xfe");
    } else if (hasBom_)
        bytes.insert(0, "\xef\xbb\xbf");
    const auto error = WriteSafely(destination, bytes);
    if (!error.empty()) return {SaveStatus::Failed, error};
    const auto previousLanguage = Language();
    if (fileType) {
        const auto* selected = std::get_if<SyntaxLanguage>(&*fileType);
        if (selected)
            languageOverride_ = *selected;
        else if (std::get<SaveType>(*fileType) == SaveType::CurrentScheme)
            languageOverride_ = previousLanguage;
        else
            languageOverride_.reset();
        // The supported default scheme is Plain Text. Original first-save semantics
        // use the filename when default, current and selected schemes are all Plain.
        if (path_.empty() && previousLanguage == SyntaxLanguage::PlainText &&
            languageOverride_ == SyntaxLanguage::PlainText)
            languageOverride_.reset();
    }
    path_ = destination;
    if (fileType && !languageOverride_) DetectAutomaticLanguage();
    // Original EditSaveFile saves the conversion without changing editor text or history.
    editor_.Send(Scintilla::Message::SetSavePoint);
    if (previousLanguage != Language()) ApplySyntaxLanguage(editor_, Language());
    return {SaveStatus::Saved, {}};
}

OpenResult DocumentSession::Open(const std::optional<wxString>& source, const std::function<bool()>& beforeReplace,
                                 std::optional<FileEncoding> encoding, std::optional<EncodingChoice> weakEncoding) {
    if (!source) return {OpenStatus::Cancelled, {}};
    using Scintilla::Message;
    // Notepad4 FileLoad compares the supplied spelling before canonicalizing the
    // load path. Do not turn distinct spellings or file-system aliases into reloads.
#ifdef __WXMSW__
    const bool samePath =
        !path_.empty() && CompareStringOrdinal(source->wc_str(), -1, path_.wc_str(), -1, TRUE) == CSTR_EQUAL;
#else
    const bool samePath = !path_.empty() && *source == path_;
#endif
    const auto caret = editor_.Send(Message::GetCurrentPos);
    const auto anchor = editor_.Send(Message::GetAnchor);
    const auto line = editor_.Send(Message::LineFromPosition, caret);
    const auto column = editor_.Send(Message::GetColumn, caret, line);
    const auto visibleTop = editor_.Send(Message::GetFirstVisibleLine);
    const auto documentTop = editor_.Send(Message::DocLineFromVisible, visibleTop);
    const auto xOffset = editor_.Send(Message::GetXOffset);
    wxFileName loadPath(*source);
    loadPath.Normalize(wxPATH_NORM_ENV_VARS | wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG |
                       wxPATH_NORM_SHORTCUT);
    wxLogNull quiet;
    std::string bytes;
    FileEncoding format;
    bool bom;
    bool systemDefault;
    const auto read = [&]() -> OpenResult {
        if (!loadPath.FileExists()) return {OpenStatus::Failed, "Choose an existing file to open."};
        wxFile file(loadPath.GetFullPath(), wxFile::read);
        if (!file.IsOpened()) return {OpenStatus::Failed, "Could not open the file for reading."};
        const auto length = file.Length();
        if (length < 0) return {OpenStatus::Failed, "Could not determine the file size."};
        bytes.assign(static_cast<size_t>(length), '\0');
        if (file.Read(bytes.data(), bytes.size()) != length || !file.Close())
            return {OpenStatus::Failed, "Could not read the complete file."};
        const auto detected = DetectEncoding(loadPath.GetFullPath(), bytes, encoding, encodingOptions_, weakEncoding);
        if (!detected.encoding) return {OpenStatus::Failed, detected.error};
        format = *detected.encoding;
        bom = detected.bom;
        systemDefault = detected.systemDefault;
        if (bom) bytes.erase(0, format == FileEncoding::Utf16Le ? 2 : 3);
        if (format == FileEncoding::Gbk) {
            const auto decoded = DecodeGbk(bytes);
            if (!decoded)
                return {OpenStatus::Failed, "The file is not valid GBK (CP936). The document was not changed."};
            bytes = *decoded;
        } else if (format == FileEncoding::Utf16Le) {
            auto decoded = DecodeUtf16Le(bytes);
            if (!decoded) return {OpenStatus::Failed, "The file is not valid UTF-16LE."};
            bytes = std::move(*decoded);
        } else if (!IsUtf8(bytes))
            return {OpenStatus::Failed, "The file is not valid UTF-8. No other encoding was assumed."};
        return {OpenStatus::Opened, {}};
    };
    auto result = read();
    if (result.status != OpenStatus::Opened) return result;
    const bool wasModified = editor_.Send(Scintilla::Message::GetModify);
    if (!beforeReplace()) return {OpenStatus::Cancelled, {}};
    // Save may have rewritten the selected source (including a newly chosen
    // first-save destination). Read it again after that explicit Save succeeds.
    if (wasModified && !editor_.Send(Scintilla::Message::GetModify)) {
        result = read();
        if (result.status != OpenStatus::Opened) return result;
    }
    Replace(bytes, loadPath.GetFullPath(), format, bom, samePath);
    systemDefaultEncoding_ = systemDefault;
    if (samePath) {
        editor_.Send(Message::SetSel, anchor, caret);
        // Original FileLoad (Notepad4 89b8b36): compare the saved one-based line
        // against the restored zero-based line, then use EditJumpTo/EditSelectEx.
        const auto displacement =
            line + 1 - editor_.Send(Message::LineFromPosition, editor_.Send(Message::GetCurrentPos));
        if (displacement > 5 || displacement < -5) {
            const auto position = line >= editor_.Send(Message::GetLineCount)
                                      ? editor_.Send(Message::GetLength)
                                      : editor_.Send(Message::FindColumn, line, column);
            constexpr auto slop = static_cast<uintptr_t>(Scintilla::CaretPolicy::Slop);
            constexpr auto strict = static_cast<uintptr_t>(Scintilla::CaretPolicy::Strict);
            constexpr auto even = static_cast<uintptr_t>(Scintilla::CaretPolicy::Even);
            editor_.Send(Message::SetXCaretPolicy, slop | strict | even, 50);
            editor_.Send(Message::SetYCaretPolicy, slop | strict | even, 5);
            editor_.Send(Message::EnsureVisibleEnforcePolicy, editor_.Send(Message::LineFromPosition, position));
            editor_.Send(Message::GotoPos, position);
            editor_.Send(Message::SetXCaretPolicy, slop | even, 50);
            editor_.Send(Message::SetYCaretPolicy, even, 0);
            editor_.Send(Message::ChooseCaretX);
        } else {
            editor_.Send(Message::EnsureVisible, documentTop);
            editor_.Send(Message::LineScroll, 0, visibleTop - editor_.Send(Message::GetFirstVisibleLine));
            editor_.Send(Message::SetXOffset, xOffset);
        }
    }
    return {OpenStatus::Opened, {}};
}

void DocumentSession::Replace(const std::string& bytes, const wxString& path, FileEncoding encoding, bool bom,
                              bool keepLanguage) {
    editor_.Send(Scintilla::Message::SetUndoCollection, 0);
    editor_.Send(Scintilla::Message::ClearAll);
    editor_.Send(Scintilla::Message::AddText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    editor_.Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(DetectEol(bytes)));
    editor_.Send(Scintilla::Message::SetUndoCollection, 1);
    editor_.Send(Scintilla::Message::EmptyUndoBuffer);
    editor_.Send(Scintilla::Message::GotoPos, 0);
    editor_.Send(Scintilla::Message::SetXOffset, 0);
    editor_.Send(Scintilla::Message::SetSavePoint);
    path_ = path;
    encoding_ = encoding;
    hasBom_ = bom;
    if (!keepLanguage) {
        languageOverride_.reset();
        DetectAutomaticLanguage();
        ApplySyntaxLanguage(editor_, Language());
    }
}

void DocumentSession::New() {
    Replace({}, {}, FileEncoding::Utf8, false);
    systemDefaultEncoding_ = false;
}

void DocumentSession::SelectLanguage(std::optional<SyntaxLanguage> language) {
    const auto previous = Language();
    languageOverride_ = language;
    if (!language) DetectAutomaticLanguage();
    if (previous != Language()) ApplySyntaxLanguage(editor_, Language());
}

void DocumentSession::DetectAutomaticLanguage() {
    // The pinned core's GetText count excludes its terminating NUL.
    // Match original SciCall_GetText(255) with the original 256-byte buffer.
    char prefix[256]{};
    editor_.Send(Scintilla::Message::GetText, sizeof(prefix) - 1, reinterpret_cast<intptr_t>(prefix));
    automaticLanguage_ = DetectLanguage(path_, prefix, detection_);
}
