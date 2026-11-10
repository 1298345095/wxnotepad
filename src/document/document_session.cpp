#include "document/document_session.h"
#include "editor/editor_control.h"
#include "syntax/syntax_scheme.h"
#include "document/encoding/gbk_codec.h"
#include "document/encoding/chinese_codec.h"
#include "document/encoding/utf16_codec.h"
#include "editor/platform/native_encoding.h"
#include "ScintillaTypes.h"
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/utils.h>
#include <cstdio>
#include <algorithm>
#ifdef __WXMSW__
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
// Original MAX_NON_UTF8_SIZE bounds EditConvertText's native conversion.
constexpr intptr_t NativeConversionLimit = (intptr_t{1} << 30) - 16;
// The original ignores query/restore failures: a successful content save stays
// successful. Preserve every settable time around the existing safe-write path.
#ifdef __WXMSW__
using FileTimes = FILE_BASIC_INFO;
std::optional<FileTimes> ReadFileTimes(const wxString& path) {
    const auto file = CreateFileW(path.wc_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    FileTimes times{};
    const bool read = GetFileInformationByHandleEx(file, FileBasicInfo, &times, sizeof(times)) != 0;
    CloseHandle(file);
    return read ? std::optional<FileTimes>(times) : std::nullopt;
}
bool SetFileTimes(const wxString& path, FileTimes times) {
    const auto file = CreateFileW(path.wc_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool set = SetFileInformationByHandle(file, FileBasicInfo, &times, sizeof(times)) != 0;
    CloseHandle(file);
    return set;
}
#else
struct FileTimes {
    timespec access;
    timespec modified;
};
std::optional<FileTimes> ReadFileTimes(const wxString& path) {
    struct stat info{};
    if (::stat(path.utf8_str(), &info) != 0) return std::nullopt;
    return FileTimes{info.st_atim, info.st_mtim};
}
bool SetFileTimes(const wxString& path, const FileTimes& times) {
    const timespec fields[]{times.access, times.modified};
    return ::utimensat(AT_FDCWD, path.utf8_str(), fields, 0) == 0;
}
#endif

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

wxString PrepareSaveCopy(const wxString& destination) {
    // Original Save Copy opens/creates the destination before cleanup and
    // legacy-loss confirmation. Cancel keeps a new empty file, while an
    // existing file retains its bytes.
    wxLogNull quiet;
    wxFile file;
    if (wxDirExists(destination)) return "Choose a file as the save destination.";
    const bool opened =
        wxFileExists(destination) ? file.Open(destination, wxFile::read_write) : file.Create(destination, false);
    if (!opened || !file.Close()) return "Could not open the destination for writing.";
    return {};
}

std::variant<std::string, SaveResult> EncodeForSave(std::string bytes, FileEncoding encoding, bool bom,
                                                    const GbkLossDecision& decideLoss) {
    if (encoding == FileEncoding::Gbk || encoding == FileEncoding::Gb18030 || encoding == FileEncoding::Big5) {
        if (!IsUtf8(bytes)) return SaveResult{SaveStatus::Failed, "The document contains invalid UTF-8."};
        const auto encoded = encoding == FileEncoding::Gbk ? EncodeGbk(bytes) : EncodeChinese(bytes, encoding);
        if (encoded.loss && (!decideLoss || decideLoss() != GbkLossChoice::Replace))
            return SaveResult{SaveStatus::Cancelled, {}};
        bytes = encoded.bytes;
    } else if (encoding == FileEncoding::Utf16Le || encoding == FileEncoding::Utf16Be) {
        auto encoded = encoding == FileEncoding::Utf16Be ? EncodeUtf16Be(bytes) : EncodeUtf16Le(bytes);
        if (!encoded)
            return SaveResult{SaveStatus::Failed, encoding == FileEncoding::Utf16Be
                                                      ? "The document could not be encoded as UTF-16BE."
                                                      : "The document could not be encoded as UTF-16LE."};
        bytes = std::move(*encoded);
        if (bom) bytes.insert(0, encoding == FileEncoding::Utf16Be ? "\xfe\xff" : "\xff\xfe");
    } else if (bom)
        bytes.insert(0, "\xef\xbb\xbf");
    return bytes;
}

void FixPositionsAfterEolConversion(EditorControl& editor) {
    using Scintilla::Message;
    const auto length = editor.Send(Message::GetLength);
    auto caret = editor.Send(Message::GetCurrentPos);
    const auto anchor = editor.Send(Message::GetAnchor);
    const auto fixPosition = [&editor](intptr_t position) {
        return editor.Send(Message::PositionAfter, editor.Send(Message::PositionBefore, position));
    };
    if (caret > 0 && caret < length) {
        const auto fixed = fixPosition(caret);
        if (fixed != caret) {
            editor.Send(Message::SetCurrentPos, fixed);
            caret = fixed;
        }
    }
    if (anchor != caret && anchor > 0 && anchor < length) {
        const auto fixed = fixPosition(anchor);
        if (fixed != anchor) editor.Send(Message::SetAnchor, fixed);
    }
}

void EnsureConsistentLineEndings(EditorControl& editor) {
    using Scintilla::Message;
    constexpr uintptr_t maxSmallFileSize = (1U << 31) - 16;
    const auto lines = static_cast<uintptr_t>(editor.Send(Message::GetLineCount));
    const auto actions = static_cast<uintptr_t>(editor.Send(Message::GetUndoActions));
    const auto mode = static_cast<uintptr_t>(editor.Send(Message::GetEOLMode));
    const bool large =
        editor.Send(Message::GetDocumentOptions) & static_cast<intptr_t>(Scintilla::DocumentOption::TextLarge);
    if (lines >= 2 && lines + actions < maxSmallFileSize &&
        (mode != static_cast<uintptr_t>(Scintilla::EndOfLine::CrLf) || large ||
         static_cast<uintptr_t>(editor.Send(Message::GetLength)) + lines < maxSmallFileSize)) {
        editor.Send(Message::ConvertEOLs, mode);
        FixPositionsAfterEolConversion(editor);
    }
}

void StripTrailingBlanks(EditorControl& editor) {
    using Scintilla::Message;
    editor.Send(Message::BeginUndoAction);
    const auto lines = editor.Send(Message::GetLineCount);
    for (intptr_t line = 0; line < lines; ++line) {
        const auto start = editor.Send(Message::PositionFromLine, line);
        const auto end = editor.Send(Message::GetLineEndPosition, line);
        auto position = end;
        while (position >= start) {
            const auto ch = editor.Send(Message::GetCharAt, --position);
            if (ch != ' ' && ch != '\t') break;
        }
        if (++position < end) editor.Send(Message::DeleteRange, position, end - position);
    }
    editor.Send(Message::EndUndoAction);
}

struct LoadedDocument {
    std::string text;
    FileEncoding encoding;
    bool bom;
    bool systemDefault;
    OriginalFileVariables variables;
};

// Preparing a candidate never changes the live document. The caller can repeat
// this read if the discard/save decision rewrites the selected source.
std::variant<LoadedDocument, wxString> ReadDocument(const wxFileName& path, std::optional<FileEncoding> encoding,
                                                    const EncodingOptions& options,
                                                    std::optional<EncodingChoice> weakEncoding) {
    if (!path.FileExists()) return wxString("Choose an existing file to open.");
    wxFile file(path.GetFullPath(), wxFile::read);
    if (!file.IsOpened()) return wxString("Could not open the file for reading.");
    const auto length = file.Length();
    if (length < 0) return wxString("Could not determine the file size.");
    LoadedDocument loaded;
    auto& bytes = loaded.text;
    bytes.assign(static_cast<size_t>(length), '\0');
    if (file.Read(bytes.data(), bytes.size()) != length || !file.Close())
        return wxString("Could not read the complete file.");
    const auto detected = DetectEncoding(path.GetFullPath(), bytes, encoding, options, weakEncoding);
    if (!detected.encoding) return detected.error;
    loaded.encoding = *detected.encoding;
    loaded.bom = detected.bom;
    loaded.systemDefault = detected.systemDefault;
    const bool utf16 = loaded.encoding == FileEncoding::Utf16Le || loaded.encoding == FileEncoding::Utf16Be;
    if (!utf16) loaded.variables = ReadOriginalFileVariables(bytes, options.noFileVariables, options.noEncodingTags);
    if (loaded.bom) bytes.erase(0, utf16 ? 2 : 3);
    if (loaded.encoding == FileEncoding::Gbk) {
        const auto decoded = DecodeGbk(bytes);
        if (!decoded) return wxString("The file is not valid GBK (CP936). The document was not changed.");
        bytes = *decoded;
    } else if (loaded.encoding == FileEncoding::Gb18030 || loaded.encoding == FileEncoding::Big5) {
        const auto decoded = DecodeChinese(bytes, loaded.encoding);
        if (!decoded) return wxString("The file could not be decoded with the selected Chinese encoding.");
        bytes = *decoded;
    } else if (utf16) {
        auto decoded = loaded.encoding == FileEncoding::Utf16Be ? DecodeUtf16Be(bytes) : DecodeUtf16Le(bytes);
        if (!decoded)
            return wxString(loaded.encoding == FileEncoding::Utf16Be ? "The file is not valid UTF-16BE."
                                                                     : "The file is not valid UTF-16LE.");
        bytes = std::move(*decoded);
        loaded.variables = ReadOriginalFileVariables(bytes, options.noFileVariables, options.noEncodingTags);
    } else if (!IsUtf8(bytes))
        return wxString("The file is not valid UTF-8. No other encoding was assumed.");
    return loaded;
}

struct ReloadView {
    intptr_t caret;
    intptr_t anchor;
    intptr_t line;
    intptr_t column;
    intptr_t visibleTop;
    intptr_t documentTop;
    intptr_t xOffset;
    intptr_t scrollWidth;
};

ReloadView CaptureReloadView(EditorControl& editor) {
    using Scintilla::Message;
    const auto caret = editor.Send(Message::GetCurrentPos);
    const auto anchor = editor.Send(Message::GetAnchor);
    const auto line = editor.Send(Message::LineFromPosition, caret);
    const auto column = editor.Send(Message::GetColumn, caret, line);
    const auto visibleTop = editor.Send(Message::GetFirstVisibleLine);
    const auto documentTop = editor.Send(Message::DocLineFromVisible, visibleTop);
    return {caret,
            anchor,
            line,
            column,
            visibleTop,
            documentTop,
            editor.Send(Message::GetXOffset),
            editor.Send(Message::GetScrollWidth)};
}

void RestoreReloadView(EditorControl& editor, const ReloadView& view) {
    using Scintilla::Message;
    // ClearAll resets width tracking. Restore the current file's measured range
    // before its view, so native layout cannot clamp the saved offset before paint.
    editor.Send(Message::SetScrollWidth, static_cast<uintptr_t>(view.scrollWidth));
    editor.Send(Message::SetSel, view.anchor, view.caret);
    // Original FileLoad compares the saved one-based line against the restored
    // zero-based line, then uses EditJumpTo/EditSelectEx.
    const auto displacement =
        view.line + 1 - editor.Send(Message::LineFromPosition, editor.Send(Message::GetCurrentPos));
    if (displacement > 5 || displacement < -5) {
        const auto position = view.line >= editor.Send(Message::GetLineCount)
                                  ? editor.Send(Message::GetLength)
                                  : editor.Send(Message::FindColumn, view.line, view.column);
        editor.JumpToPosition(position);
    } else {
        editor.Send(Message::EnsureVisible, view.documentTop);
        editor.Send(Message::LineScroll, 0, view.visibleTop - editor.Send(Message::GetFirstVisibleLine));
        editor.Send(Message::SetXOffset, view.xOffset);
    }
}
} // namespace

wxString DocumentSession::FileNameText(FileNamePart part) const {
    wxString text = path_.empty()                    ? wxString("Untitled")
                    : part == FileNamePart::FullPath ? path_
                                                     : wxFileName(path_).GetFullName();
    if (part == FileNamePart::BaseName) {
        // Pinned Notepad4 uses PathRemoveExtension, including leading dots and
        // its reset at a space or backslash; wxFileName::GetName differs.
        const auto dot = text.find_last_of('.');
        const auto boundary = text.find_last_of(" \\");
        if (dot != wxString::npos && (boundary == wxString::npos || dot > boundary)) text.Truncate(dot);
    }
    return text;
}

bool DocumentSession::RefreshReadOnlyFile() {
    readOnlyFile_ = false;
    if (path_.empty()) return false;
#ifdef __WXMSW__
    const auto attributes = GetFileAttributesW(path_.wc_str());
    readOnlyFile_ = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0;
#else
    readOnlyFile_ = wxFileExists(path_) && !wxFileName::IsFileWritable(path_);
#endif
    return readOnlyFile_;
}

SaveResult DocumentSession::SaveFirst(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss,
                                      SaveFileType fileType, SaveTimeMode timeMode) {
    if (!destination) return {SaveStatus::Cancelled, {}};
    if (!path_.empty()) return {SaveStatus::Failed, "This document already has a file name."};
    if (destination->empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(*destination, decideLoss, fileType, false, timeMode);
}

SaveResult DocumentSession::Save(const GbkLossDecision& decideLoss, SaveTimeMode timeMode) {
    if (path_.empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(path_, decideLoss, std::nullopt, false, timeMode);
}

SaveResult DocumentSession::SaveAs(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss,
                                   SaveFileType fileType, SaveTimeMode timeMode) {
    if (!destination) return {SaveStatus::Cancelled, {}};
    if (destination->empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(*destination, decideLoss, fileType, false, timeMode);
}

SaveResult DocumentSession::SaveCopy(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss) {
    if (!destination) return {SaveStatus::Cancelled, {}};
    if (destination->empty()) return {SaveStatus::Failed, "Choose a file name before saving."};
    return SaveTo(*destination, decideLoss, std::nullopt, true);
}

SaveResult DocumentSession::SaveTo(const wxString& destination, const GbkLossDecision& decideLoss,
                                   std::optional<SaveFileType> fileType, bool copy, SaveTimeMode timeMode) {
    if (copy) {
        const auto error = PrepareSaveCopy(destination);
        if (!error.empty()) return {SaveStatus::Failed, error};
    }
    PendingSave temporary;
    std::optional<FileTimes> originalTimes;
    if (timeMode == SaveTimeMode::Original) {
        const auto error = PrepareSave(destination, temporary);
        if (!error.empty()) return {SaveStatus::Failed, error};
        // OPEN_ALWAYS in the original captures a new file's initial timestamps.
        // Our new file is the safe-write sibling until replacement commits it.
        originalTimes = ReadFileTimes(wxFileExists(destination) ? destination : temporary.path);
    }
    // Original EditSaveFile opens the destination before changing text. Prepare
    // the safe-write sibling at that point, without truncating the destination.
    if ((saveCleanup_.fixLineEndings || saveCleanup_.fixTrailingBlanks) &&
        !editor_.Send(Scintilla::Message::GetReadOnly)) {
        if (temporary.path.empty()) {
            const auto error = PrepareSave(destination, temporary);
            if (!error.empty()) return {SaveStatus::Failed, error};
        }
        ApplySaveCleanup();
    }
    const auto length = static_cast<size_t>(editor_.Send(Scintilla::Message::GetLength));
    std::string bytes(length + 1, '\0');
    editor_.Send(Scintilla::Message::GetText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    bytes.resize(length);
    const auto encoded = EncodeForSave(std::move(bytes), encoding_, hasBom_, decideLoss);
    if (const auto* result = std::get_if<SaveResult>(&encoded)) return *result;
    const auto error = WriteSafely(destination, std::get<std::string>(encoded), temporary);
    if (!error.empty()) return {SaveStatus::Failed, error};
    if (originalTimes) SetFileTimes(destination, *originalTimes);
    // Original EditSaveFile/FileSave leave the live identity, scheme and save
    // point alone for Save Copy; optional cleanup above remains in its history.
    if (copy) return {SaveStatus::Saved, {}};
    CommitSaveState(destination, fileType);
    return {SaveStatus::Saved, {}};
}

void DocumentSession::CommitSaveState(const wxString& destination, std::optional<SaveFileType> fileType) {
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
    originalEncoding_ = CurrentEncoding();
    RefreshReadOnlyFile();
    if (fileType && !languageOverride_) DetectAutomaticLanguage();
    // Original EditSaveFile saves the conversion without changing editor text or history.
    editor_.Send(Scintilla::Message::SetSavePoint);
    if (previousLanguage != Language()) {
        ApplyEmptySchemeDefaults();
        ApplySyntaxLanguage(editor_, Language());
    }
}

bool DocumentSession::UpdateFileTime() {
    if (path_.empty()) return false;
#ifdef __WXMSW__
    const auto file = CreateFileW(path_.wc_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    FILE_BASIC_INFO times{};
    bool updated = GetFileInformationByHandleEx(file, FileBasicInfo, &times, sizeof(times)) != 0;
    if (updated) {
        GetSystemTimePreciseAsFileTime(reinterpret_cast<FILETIME*>(&times.LastWriteTime));
        updated = SetFileInformationByHandle(file, FileBasicInfo, &times, sizeof(times)) != 0;
    }
    CloseHandle(file);
#else
    const int file = ::open(path_.utf8_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (file < 0) return false;
    // Preserve access time; ctime follows the filesystem's metadata-update rules.
    const timespec times[]{{0, UTIME_OMIT}, {0, UTIME_NOW}};
    const bool updated = ::futimens(file, times) == 0;
    ::close(file);
#endif
    return updated;
}

void DocumentSession::ApplySaveCleanup() {
    // Adapted from pinned Notepad4 EditEnsureConsistentLineEndings,
    // EditFixPositions and EditStripTrailingBlanks(true), in their save order.
    // See licenses/Notepad4.txt.
    if (saveCleanup_.fixLineEndings) EnsureConsistentLineEndings(editor_);
    if (saveCleanup_.fixTrailingBlanks) StripTrailingBlanks(editor_);
}

OpenResult DocumentSession::Open(const std::optional<wxString>& source, const std::function<bool()>& beforeReplace,
                                 std::optional<FileEncoding> encoding, std::optional<EncodingChoice> weakEncoding,
                                 const std::function<void()>& configureLoadedDocument,
                                 OpenDecisionTiming decisionTiming) {
    if (!source) return {OpenStatus::Cancelled, {}};
    // Notepad4 FileLoad compares the supplied spelling before canonicalizing the
    // load path. Do not turn distinct spellings or file-system aliases into reloads.
#ifdef __WXMSW__
    const bool samePath =
        !path_.empty() && CompareStringOrdinal(source->wc_str(), -1, path_.wc_str(), -1, TRUE) == CSTR_EQUAL;
#else
    const bool samePath = !path_.empty() && *source == path_;
#endif
    const auto view = CaptureReloadView(editor_);
    const bool keepReadOnly = samePath && editor_.Send(Scintilla::Message::GetReadOnly);
    wxFileName loadPath(*source);
    loadPath.Normalize(wxPATH_NORM_ENV_VARS | wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG |
                       wxPATH_NORM_SHORTCUT);
    wxLogNull quiet;
    const bool wasModified = IsModified();
    if (decisionTiming == OpenDecisionTiming::BeforeRead && !beforeReplace()) return {OpenStatus::Cancelled, {}};
    auto candidate = ReadDocument(loadPath, encoding, encodingOptions_, weakEncoding);
    if (const auto* error = std::get_if<wxString>(&candidate)) return {OpenStatus::Failed, *error};
    if (decisionTiming == OpenDecisionTiming::AfterRead && !beforeReplace()) return {OpenStatus::Cancelled, {}};
    // Save may have rewritten the selected source (including a newly chosen
    // first-save destination). Read it again after that explicit Save succeeds.
    if (decisionTiming == OpenDecisionTiming::AfterRead && wasModified && !IsModified()) {
        // Release the old decoded text before allocating another whole-file read.
        candidate.emplace<wxString>();
        candidate = ReadDocument(loadPath, encoding, encodingOptions_, weakEncoding);
        if (const auto* error = std::get_if<wxString>(&candidate)) return {OpenStatus::Failed, *error};
    }
    auto& loaded = std::get<LoadedDocument>(candidate);
    const auto result = Replace(loaded.text, loadPath.GetFullPath(), loaded.encoding, loaded.bom, samePath);
    systemDefaultEncoding_ = loaded.systemDefault && !(!samePath && loaded.text.empty() && !loaded.bom &&
                                                       SyntaxSettingsLanguage(Language()) == SyntaxLanguage::Bash);
    originalEncoding_ = CurrentEncoding();
    fileVariables_ = std::move(loaded.variables);
    // FileLoad applies the loaded file's indentation before restoring its view
    // or resolving a saved visual column against the new tab width.
    if (configureLoadedDocument) configureLoadedDocument();
    if (keepReadOnly || IsReadOnlyFile()) editor_.Send(Scintilla::Message::SetReadOnly, 1);
    if (samePath) RestoreReloadView(editor_, view);
    return result;
}

OpenResult DocumentSession::Replace(const std::string& bytes, const wxString& path, FileEncoding encoding, bool bom,
                                    bool keepLanguage) {
    const auto lineEndings = CountLineEndings(bytes);
    editor_.Send(Scintilla::Message::SetReadOnly, 0);
    const auto changeHistory = editor_.Send(Scintilla::Message::GetChangeHistory);
    editor_.Send(Scintilla::Message::SetChangeHistory, 0);
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
    editor_.Send(Scintilla::Message::SetChangeHistory, changeHistory);
    editor_.Send(Scintilla::Message::SetUndoSelectionHistory, selectionHistory);
    path_ = path;
    RefreshReadOnlyFile();
    encoding_ = encoding;
    hasBom_ = bom;
    bool unknownLanguage = false;
    if (!keepLanguage) {
        languageOverride_.reset();
        unknownLanguage = !DetectAutomaticLanguage();
        ApplyEmptySchemeDefaults();
        ApplySyntaxLanguage(editor_, Language());
    }
    return {OpenStatus::Opened, {}, lineEndings, unknownLanguage};
}

void DocumentSession::New() {
    Replace({}, {}, FileEncoding::Utf8, false);
    systemDefaultEncoding_ = false;
    originalEncoding_ = CurrentEncoding();
    fileVariables_ = {};
}

bool DocumentSession::IsModified() const {
    return editor_.Send(Scintilla::Message::GetModify) || originalEncoding_ != CurrentEncoding();
}

bool DocumentSession::SelectEncoding(EncodingChoice choice) {
    // Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
    if (choice == CurrentEncoding()) return false;
    const bool crossesAnsi = choice.systemDefault != systemDefaultEncoding_;
    const auto length = editor_.Send(Scintilla::Message::GetLength);
    // Pinned EditConvertText changes the native buffer, discarding its history.
    // This adapter keeps the original core in UTF-8 and round-trips native text
    // at that boundary. UTF-8 native locales need no buffer conversion, as in
    // the original's cpSource == cpDest early return.
    const auto native = NativeEncodingChoice();
    if (crossesAnsi && native && native->encoding != FileEncoding::Utf8 && length < NativeConversionLimit) {
        using Scintilla::Message;
        std::string text(static_cast<size_t>(length) + 1, '\0');
        editor_.Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
        text.resize(static_cast<size_t>(length));
        if (choice.systemDefault)
            text = NativeDefaultEncodingText(wxString::FromUTF8(text.data(), text.size())).utf8_string();
        const auto history = editor_.Send(Message::GetChangeHistory);
        const auto selectionHistory = editor_.Send(Message::GetUndoSelectionHistory);
        editor_.Send(Message::SetReadOnly, 0);
        editor_.Send(Message::Cancel);
        editor_.Send(Message::SetChangeHistory, 0);
        editor_.Send(Message::SetUndoSelectionHistory, 0);
        editor_.Send(Message::SetUndoCollection, 0);
        editor_.Send(Message::EmptyUndoBuffer);
        editor_.Send(Message::ClearAll);
        editor_.Send(Message::MarkerDeleteAll, static_cast<uintptr_t>(-1));
        editor_.Send(Message::AppendText, text.size(), reinterpret_cast<intptr_t>(text.data()));
        editor_.Send(Message::EmptyUndoBuffer);
        editor_.Send(Message::SetUndoCollection, 1);
        if (!length && path_.empty()) editor_.Send(Message::SetSavePoint);
        editor_.Send(Message::SetChangeHistory, history);
        editor_.Send(Message::SetUndoSelectionHistory, selectionHistory);
    }
    // Notepad4 89b8b36 Notepad4.cpp IDM_ENCODING_SELECT: an empty
    // Untitled document adopts the format; a named one compares BOM presence.
    // The original core continues to own text, selection and Undo/Redo.
    if (editor_.Send(Scintilla::Message::GetLength) == 0 &&
        (path_.empty() || choice.bom == (originalEncoding_ && originalEncoding_->bom)))
        originalEncoding_ = choice;
    if (length && crossesAnsi) originalEncoding_.reset();
    encoding_ = choice.encoding;
    hasBom_ = choice.bom;
    systemDefaultEncoding_ = choice.systemDefault;
    return true;
}

void DocumentSession::SelectLanguage(std::optional<SyntaxLanguage> language) {
    const auto previous = Language();
    languageOverride_ = language;
    if (!language) DetectAutomaticLanguage();
    if (previous != Language()) {
        ApplyEmptySchemeDefaults();
        ApplySyntaxLanguage(editor_, Language());
    }
}

void DocumentSession::ApplyEmptySchemeDefaults() {
    // Pinned Styles.cpp::Style_SetLexer / Notepad4.cpp::EditApplyDefaultEncoding.
    // Undo action count includes pending Redo; clean-but-edited empties retain
    // their current encoding/EOL. Scheme defaults preserve an existing BOM.
    using Scintilla::Message;
    if (editor_.Send(Message::GetLength) || editor_.Send(Message::GetUndoActions)) return;
    const bool shell = SyntaxSettingsLanguage(Language()) == SyntaxLanguage::Bash;
    editor_.Send(Message::SetEOLMode, static_cast<uintptr_t>(shell ? Scintilla::EndOfLine::Lf : defaultEolMode_));
    if (shell && !hasBom_ && CurrentEncoding() != EncodingChoice(FileEncoding::Utf8)) {
        encoding_ = FileEncoding::Utf8;
        systemDefaultEncoding_ = false;
        originalEncoding_ = CurrentEncoding();
    }
}

bool DocumentSession::DetectAutomaticLanguage() {
    // The pinned core's GetText count excludes its terminating NUL.
    // Match original SciCall_GetText(255) with the original 256-byte buffer.
    // Style_GetDocTypeLanguage inspects a larger header than the 255-byte
    // filename/shebang/markup guess. DetectLanguage retains both limits.
    char prefix[4000]{};
    editor_.Send(Scintilla::Message::GetText, sizeof(prefix) - 1, reinterpret_cast<intptr_t>(prefix));
    const auto detected = DetectLanguage(path_, prefix, detection_);
    automaticLanguage_ = detected.value_or(SyntaxLanguage::PlainText);
    return detected.has_value();
}
