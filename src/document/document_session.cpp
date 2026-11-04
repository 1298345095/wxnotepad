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
#include <algorithm>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
LineEndingCounts CountLineEndings(std::string_view bytes) {
    LineEndingCounts counts;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '\r') {
            if (i + 1 < bytes.size() && bytes[i + 1] == '\n') {
                ++counts.crlf;
                ++i;
            } else
                ++counts.cr;
        } else if (bytes[i] == '\n')
            ++counts.lf;
    }
    return counts;
}

Scintilla::EndOfLine SelectEol(const LineEndingCounts& counts, Scintilla::EndOfLine configured) {
    // Notepad4 89b8b36 EditDetectEOLMode: retain the configured mode when tied,
    // otherwise choose the largest count, preferring CRLF and then LF.
    const size_t maximum = std::max({counts.crlf, counts.cr, counts.lf});
    const size_t preferred = configured == Scintilla::EndOfLine::CrLf ? counts.crlf
                             : configured == Scintilla::EndOfLine::Lf ? counts.lf
                                                                      : counts.cr;
    if (preferred == maximum) return configured;
    if (counts.crlf == maximum) return Scintilla::EndOfLine::CrLf;
    return counts.lf == maximum ? Scintilla::EndOfLine::Lf : Scintilla::EndOfLine::Cr;
}

struct PendingSave {
    wxString path;
    wxFile file;
    ~PendingSave() {
        file.Close();
        if (!path.empty()) wxRemoveFile(path);
    }
};

wxString PrepareSave(const wxString& destination, PendingSave& temporary) {
    // Errors are returned to the application instead of opening wx log dialogs.
    wxLogNull quiet;
    if (wxDirExists(destination)) return "Choose a file as the save destination.";
    if (wxFileExists(destination)) {
        wxFile existing(destination, wxFile::read_write);
        if (!existing.IsOpened()) return "Could not open the destination for writing.";
    }
    temporary.path = wxFileName::CreateTempFileName(destination + ".wxnotepad-", &temporary.file);
    if (temporary.path.empty())
        return "Could not create a temporary file beside the destination. Check the folder and access permissions.";
    return {};
}

wxString WriteSafely(const wxString& destination, const std::string& bytes, PendingSave& temporary) {
    wxLogNull quiet;
    if (temporary.path.empty()) {
        const auto error = PrepareSave(destination, temporary);
        if (!error.empty()) return error;
    }
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
    PendingSave temporary;
    // Original EditSaveFile opens the destination before changing text. Prepare
    // the safe-write sibling at that point, without truncating the destination.
    if ((saveCleanup_.fixLineEndings || saveCleanup_.fixTrailingBlanks) &&
        !editor_.Send(Scintilla::Message::GetReadOnly)) {
        const auto error = PrepareSave(destination, temporary);
        if (!error.empty()) return {SaveStatus::Failed, error};
        ApplySaveCleanup();
    }
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
    const auto error = WriteSafely(destination, bytes, temporary);
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

void DocumentSession::ApplySaveCleanup() {
    using Scintilla::Message;
    // Adapted from pinned Notepad4 EditEnsureConsistentLineEndings,
    // EditFixPositions and EditStripTrailingBlanks(true), in their save order.
    // See licenses/Notepad4.txt.
    if (saveCleanup_.fixLineEndings) {
        constexpr uintptr_t maxSmallFileSize = (1U << 31) - 16;
        const auto lines = static_cast<uintptr_t>(editor_.Send(Message::GetLineCount));
        const auto actions = static_cast<uintptr_t>(editor_.Send(Message::GetUndoActions));
        const auto mode = static_cast<uintptr_t>(editor_.Send(Message::GetEOLMode));
        const bool large =
            editor_.Send(Message::GetDocumentOptions) & static_cast<intptr_t>(Scintilla::DocumentOption::TextLarge);
        if (lines >= 2 && lines + actions < maxSmallFileSize &&
            (mode != static_cast<uintptr_t>(Scintilla::EndOfLine::CrLf) || large ||
             static_cast<uintptr_t>(editor_.Send(Message::GetLength)) + lines < maxSmallFileSize)) {
            editor_.Send(Message::ConvertEOLs, mode);
            const auto length = editor_.Send(Message::GetLength);
            auto caret = editor_.Send(Message::GetCurrentPos);
            const auto anchor = editor_.Send(Message::GetAnchor);
            const auto fixPosition = [&](intptr_t position) {
                return editor_.Send(Message::PositionAfter, editor_.Send(Message::PositionBefore, position));
            };
            if (caret > 0 && caret < length) {
                const auto fixed = fixPosition(caret);
                if (fixed != caret) {
                    editor_.Send(Message::SetCurrentPos, fixed);
                    caret = fixed;
                }
            }
            if (anchor != caret && anchor > 0 && anchor < length) {
                const auto fixed = fixPosition(anchor);
                if (fixed != anchor) editor_.Send(Message::SetAnchor, fixed);
            }
        }
    }
    if (saveCleanup_.fixTrailingBlanks) {
        editor_.Send(Message::BeginUndoAction);
        const auto lines = editor_.Send(Message::GetLineCount);
        for (intptr_t line = 0; line < lines; ++line) {
            const auto start = editor_.Send(Message::PositionFromLine, line);
            const auto end = editor_.Send(Message::GetLineEndPosition, line);
            auto position = end;
            while (position >= start) {
                const auto ch = editor_.Send(Message::GetCharAt, --position);
                if (ch != ' ' && ch != '\t') break;
            }
            if (++position < end) editor_.Send(Message::DeleteRange, position, end - position);
        }
        editor_.Send(Message::EndUndoAction);
    }
}

OpenResult DocumentSession::Open(const std::optional<wxString>& source, const std::function<bool()>& beforeReplace,
                                 std::optional<FileEncoding> encoding, std::optional<EncodingChoice> weakEncoding,
                                 const std::function<void()>& configureLoadedDocument) {
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
    OriginalFileVariables variables;
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
        if (format != FileEncoding::Utf16Le)
            variables =
                ReadOriginalFileVariables(bytes, encodingOptions_.noFileVariables, encodingOptions_.noEncodingTags);
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
            variables =
                ReadOriginalFileVariables(bytes, encodingOptions_.noFileVariables, encodingOptions_.noEncodingTags);
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
    const auto loaded = Replace(bytes, loadPath.GetFullPath(), format, bom, samePath);
    systemDefaultEncoding_ = systemDefault;
    fileVariables_ = std::move(variables);
    // FileLoad applies the loaded file's indentation before restoring its view
    // or resolving a saved visual column against the new tab width.
    if (configureLoadedDocument) configureLoadedDocument();
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
            editor_.JumpToPosition(position);
        } else {
            editor_.Send(Message::EnsureVisible, documentTop);
            editor_.Send(Message::LineScroll, 0, visibleTop - editor_.Send(Message::GetFirstVisibleLine));
            editor_.Send(Message::SetXOffset, xOffset);
        }
    }
    return loaded;
}

OpenResult DocumentSession::Replace(const std::string& bytes, const wxString& path, FileEncoding encoding, bool bom,
                                    bool keepLanguage) {
    const auto lineEndings = CountLineEndings(bytes);
    const auto selectionHistory = editor_.Send(Scintilla::Message::GetUndoSelectionHistory);
    editor_.Send(Scintilla::Message::SetUndoSelectionHistory, 0);
    editor_.Send(Scintilla::Message::SetUndoCollection, 0);
    editor_.Send(Scintilla::Message::ClearAll);
    editor_.Send(Scintilla::Message::MarkerDeleteAll, 0);
    editor_.Send(Scintilla::Message::AddText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    editor_.Send(Scintilla::Message::SetEOLMode, static_cast<uintptr_t>(SelectEol(lineEndings, defaultEolMode_)));
    editor_.Send(Scintilla::Message::SetUndoCollection, 1);
    editor_.Send(Scintilla::Message::EmptyUndoBuffer);
    editor_.Send(Scintilla::Message::GotoPos, 0);
    editor_.Send(Scintilla::Message::SetXOffset, 0);
    editor_.Send(Scintilla::Message::SetSavePoint);
    editor_.Send(Scintilla::Message::SetUndoSelectionHistory, selectionHistory);
    path_ = path;
    encoding_ = encoding;
    hasBom_ = bom;
    bool unknownLanguage = false;
    if (!keepLanguage) {
        languageOverride_.reset();
        unknownLanguage = !DetectAutomaticLanguage();
        ApplySyntaxLanguage(editor_, Language());
    }
    return {OpenStatus::Opened, {}, lineEndings, unknownLanguage};
}

void DocumentSession::New() {
    Replace({}, {}, FileEncoding::Utf8, false);
    systemDefaultEncoding_ = false;
    fileVariables_ = {};
}

void DocumentSession::SelectLanguage(std::optional<SyntaxLanguage> language) {
    const auto previous = Language();
    languageOverride_ = language;
    if (!language) DetectAutomaticLanguage();
    if (previous != Language()) ApplySyntaxLanguage(editor_, Language());
}

bool DocumentSession::DetectAutomaticLanguage() {
    // The pinned core's GetText count excludes its terminating NUL.
    // Match original SciCall_GetText(255) with the original 256-byte buffer.
    char prefix[256]{};
    editor_.Send(Scintilla::Message::GetText, sizeof(prefix) - 1, reinterpret_cast<intptr_t>(prefix));
    const auto detected = DetectLanguage(path_, prefix, detection_);
    automaticLanguage_ = detected.value_or(SyntaxLanguage::PlainText);
    return detected.has_value();
}
