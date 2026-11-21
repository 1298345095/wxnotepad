#include "document/document_session.h"
#include <utility>
#include "editor/editor_control.h"
#include "editor/editor_ids.h"
#include "syntax/syntax_scheme.h"
#include "document/encoding/gbk_codec.h"
#include "document/encoding/chinese_codec.h"
#include "document/encoding/utf16_codec.h"
#include "document/encoding/single_byte_codec.h"
#include "editor/platform/native_encoding.h"
#include "ScintillaTypes.h"
#include "UniConversion.h"
#include <wx/translation.h>
#include <wx/datetime.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <cstdio>
#include <algorithm>
#include <array>
#include <limits>
#ifdef __WXMSW__
#include <filesystem>
#include <shlobj.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
wxString BackupPath(const wxString& source, bool automatic) {
    // Notepad4 extracts from the first dot of the full path, then removes
    // the automatic timestamp suffix at the last space in the constructed name.
    const auto dot = source.find('.');
    const auto extension = dot == wxString::npos ? wxString(".bak") : source.Mid(dot);
    const auto time = wxDateTime::UNow();
    auto backup = source + time.Format(" %Y%m%d %H%M%S");
    if (automatic)
        return backup + wxString::Format(" %03u%lu", static_cast<unsigned>(time.GetMillisecond()), wxGetProcessId()) +
               extension;
    if (const auto space = extension.rfind(' '); space != wxString::npos)
        backup += wxString::Format(" %03u%lu", static_cast<unsigned>(time.GetMillisecond()), wxGetProcessId()) +
                  extension.Left(space);
    return backup + extension;
}

wxString DefaultBackupDirectory() {
    wxFileName directory;
#ifdef __WXMSW__
    PWSTR local = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local) != S_OK)
        return wxFileName(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(), "AutoSave").GetFullPath();
    directory = wxFileName::DirName(local);
    CoTaskMemFree(local);
#else
    wxString root;
    if (wxGetEnv("XDG_DATA_HOME", &root) && wxFileName(root).IsAbsolute()) {
        directory = wxFileName::DirName(root);
    } else {
        directory = wxFileName::DirName(wxGetHomeDir());
        directory.AppendDir(".local");
        directory.AppendDir("share");
    }
#endif
    directory.AppendDir("Notepad4");
    directory.AppendDir("AutoSave");
    return directory.GetPath();
}

std::string_view BackupMetadataEnding(Scintilla::EndOfLine mode) {
    using enum Scintilla::EndOfLine;
    if (mode == Lf) return "\n";
    if (mode == Cr) return "\r";
    return "\r\n";
}

enum class BackupCreation { OpenFailed, WriteFailed, Complete };

BackupCreation CreateBackup(const wxString& path, std::string_view bytes) {
#ifdef __WXMSW__
    const auto file = CreateFileW(path.wc_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return BackupCreation::OpenFailed;
    DWORD written = 0;
    const bool complete = bytes.size() <= MAXDWORD &&
                          WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                          written == bytes.size();
    CloseHandle(file);
#else
    wxFile file;
    if (!file.Create(path, false)) return BackupCreation::OpenFailed;
    const bool written = file.Write(bytes.data(), bytes.size()) == bytes.size();
    const bool complete = file.Close() && written;
#endif
    // Only a failure to create the file permits the caller to try a fallback.
    if (!complete) wxRemoveFile(path);
    return complete ? BackupCreation::Complete : BackupCreation::WriteFailed;
}

// Original MAX_NON_UTF8_SIZE bounds EditConvertText's native conversion.
constexpr intptr_t NativeConversionLimit = (intptr_t{1} << 30) - 16;
// The original ignores query/restore failures: a successful content save stays
// successful. Preserve every settable time around the existing safe-write path.
#ifdef __WXMSW__
using FileTimes = FILE_BASIC_INFO;
std::optional<FILETIME> ReadAccessTime(const wxString& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.wc_str(), GetFileExInfoStandard, &attributes)) return std::nullopt;
    if (attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        // Path attributes describe a final symbolic link itself; the command's
        // normal CreateFile follows it. Capture the same target's access time.
        std::error_code error;
        const auto target = std::filesystem::canonical(std::filesystem::path(path.ToStdWstring()), error);
        if (error || !GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &attributes)) return std::nullopt;
    }
    return attributes.ftLastAccessTime;
}
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
    const std::array fields{times.access, times.modified};
    return ::utimensat(AT_FDCWD, path.utf8_str(), fields.data(), 0) == 0;
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
    using enum Scintilla::EndOfLine;
    // Notepad4 89b8b36 EditDetectEOLMode: retain the configured mode when tied,
    // otherwise choose the largest count, preferring CRLF and then LF.
    const size_t maximum = std::max({counts.crlf, counts.cr, counts.lf});
    auto preferred = counts.cr;
    if (configured == CrLf)
        preferred = counts.crlf;
    else if (configured == Lf)
        preferred = counts.lf;
    if (preferred == maximum) return configured;
    if (counts.crlf == maximum) return CrLf;
    return counts.lf == maximum ? Lf : Cr;
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
    if (wxDirExists(destination)) return _("Choose a file as the save destination.");
    if (wxFileExists(destination)) {
        wxFile existing(destination, wxFile::read_write);
        if (!existing.IsOpened()) return _("Could not open the destination for writing.");
    }
    temporary.path = wxFileName::CreateTempFileName(destination + ".wxnotepad-", &temporary.file);
    if (temporary.path.empty())
        return _("Could not create a temporary file beside the destination. Check the folder and access permissions.");
    return {};
}

wxString WriteSafely(const wxString& destination, const std::string& bytes, PendingSave& temporary) {
    wxLogNull quiet;
    if (temporary.path.empty()) {
        const auto error = PrepareSave(destination, temporary);
        if (!error.empty()) return error;
    }
    if (temporary.file.Write(bytes.data(), bytes.size()) != bytes.size() || !temporary.file.Flush())
        return _("Could not write the complete file. Check free space and access permissions.");
    if (!temporary.file.Close()) return _("Could not close the completed temporary file.");
#ifdef __WXMSW__
    const bool replaced = MoveFileExW(temporary.path.wc_str(), destination.wc_str(),
                                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    const bool replaced = ::rename(temporary.path.utf8_str(), destination.utf8_str()) == 0;
#endif
    if (!replaced) {
        const wxString detail(wxSysErrorMsg());
        return wxString::Format(_("Could not replace the destination: %s"), detail);
    }
    temporary.path.clear();
    return {};
}

wxString PrepareSaveCopy(const wxString& destination) {
    // Original Save Copy opens/creates the destination before cleanup and
    // legacy-loss confirmation. Cancel keeps a new empty file, while an
    // existing file retains its bytes.
    wxLogNull quiet;
    wxFile file;
    if (wxDirExists(destination)) return _("Choose a file as the save destination.");
    const bool opened =
        wxFileExists(destination) ? file.Open(destination, wxFile::read_write) : file.Create(destination, false);
    if (!opened || !file.Close()) return _("Could not open the destination for writing.");
    return {};
}

LegacyEncoded EncodeLegacy(std::string_view bytes, FileEncoding encoding) {
    using enum FileEncoding;
    if (encoding == Gbk) return EncodeGbk(bytes);
    if (encoding == Windows1252 || encoding == Dos437) return EncodeSingleByte(bytes, encoding);
    return EncodeChinese(bytes, encoding);
}

LegacyEncoded EncodeUnmarkedNativeText(std::string_view bytes, FileEncoding encoding) {
    // Notepad4 EditSaveFile writes CPI_DEFAULT bytes without conversion. The
    // migrated buffer holds Unicode text plus any undecodable native bytes
    // produced by Base64 Decode. Retain those bytes while encoding text runs.
    LegacyEncoded result{{}, false};
    const auto appendText = [&](std::string_view text) {
        const auto encoded = EncodeLegacy(text, encoding);
        result.bytes += encoded.bytes;
        result.loss |= encoded.loss;
    };
    size_t run = 0;
    for (size_t offset = 0; offset < bytes.size();) {
        const auto status = Scintilla::Internal::UTF8Classify(bytes.substr(offset));
        if (status & Scintilla::Internal::UTF8MaskInvalid) {
            if (run != offset) appendText(bytes.substr(run, offset - run));
            result.bytes += bytes[offset++];
            run = offset;
        } else {
            offset += static_cast<size_t>(status & Scintilla::Internal::UTF8MaskWidth);
        }
    }
    if (run != bytes.size()) appendText(bytes.substr(run));
    return result;
}

LegacyEncoded EncodeNativeBuffer(std::string_view bytes, FileEncoding encoding, const NativeByteRanges& raw) {
    LegacyEncoded result{{}, false};
    const auto appendText = [&](std::string_view text) {
        const auto encoded = EncodeUnmarkedNativeText(text, encoding);
        result.bytes += encoded.bytes;
        result.loss |= encoded.loss;
    };
    intptr_t position = 0;
    for (const auto& range : raw) {
        if (position < range.start)
            appendText(bytes.substr(static_cast<size_t>(position), static_cast<size_t>(range.start - position)));
        result.bytes += bytes.substr(static_cast<size_t>(range.start), static_cast<size_t>(range.end - range.start));
        position = range.end;
    }
    if (position < static_cast<intptr_t>(bytes.size())) appendText(bytes.substr(static_cast<size_t>(position)));
    return result;
}

std::variant<std::string, SaveResult> EncodeForSave(std::string bytes, FileEncoding encoding, bool bom,
                                                    bool nativeDefault, const NativeByteRanges& raw,
                                                    const EncodingLossDecision& decideLoss) {
    using enum FileEncoding;
    if (encoding == Gbk || encoding == Windows1252 || encoding == Dos437 || encoding == Gb18030 || encoding == Big5) {
        const bool validUtf8 = IsUtf8(bytes);
        if (!validUtf8 && !nativeDefault)
            return SaveResult{SaveStatus::Failed, _("The document contains invalid UTF-8.")};
        const auto encoded = nativeDefault && (!validUtf8 || !raw.empty()) ? EncodeNativeBuffer(bytes, encoding, raw)
                                                                           : EncodeLegacy(bytes, encoding);
        if (encoded.loss && (!decideLoss || decideLoss() != EncodingLossChoice::Replace))
            return SaveResult{SaveStatus::Cancelled, {}};
        bytes = encoded.bytes;
    } else if (encoding == Utf16Le || encoding == Utf16Be) {
        auto encoded = encoding == Utf16Be ? EncodeUtf16Be(bytes) : EncodeUtf16Le(bytes);
        if (!encoded)
            return SaveResult{SaveStatus::Failed, encoding == Utf16Be
                                                      ? _("The document could not be encoded as UTF-16BE.")
                                                      : _("The document could not be encoded as UTF-16LE.")};
        bytes = std::move(*encoded);
        if (bom) bytes.insert(0, encoding == Utf16Be ? "\x{fe}\x{ff}" : "\x{ff}\x{fe}");
    } else if (bom)
        bytes.insert(0, "\x{ef}\x{bb}\x{bf}");
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
    using enum Scintilla::Message;
    constexpr uintptr_t maxSmallFileSize = (1U << 31) - 16;
    const auto lines = static_cast<uintptr_t>(editor.Send(GetLineCount));
    const auto actions = static_cast<uintptr_t>(editor.Send(GetUndoActions));
    const auto mode = static_cast<uintptr_t>(editor.Send(GetEOLMode));
    const bool large = editor.Send(GetDocumentOptions) &
                       static_cast<intptr_t>(std::to_underlying(Scintilla::DocumentOption::TextLarge));
    if (lines >= 2 && lines + actions < maxSmallFileSize &&
        (mode != static_cast<uintptr_t>(std::to_underlying(Scintilla::EndOfLine::CrLf)) || large ||
         static_cast<uintptr_t>(editor.Send(GetLength)) + lines < maxSmallFileSize)) {
        editor.Send(ConvertEOLs, mode);
        FixPositionsAfterEolConversion(editor);
    }
}

void StripTrailingBlanks(EditorControl& editor) {
    using enum Scintilla::Message;
    editor.Send(BeginUndoAction);
    const auto lines = editor.Send(GetLineCount);
    for (intptr_t line = 0; line < lines; ++line) {
        const auto start = editor.Send(PositionFromLine, line);
        const auto end = editor.Send(GetLineEndPosition, line);
        auto position = end;
        while (position >= start) {
            const auto ch = editor.Send(GetCharAt, --position);
            if (ch != ' ' && ch != '\t') break;
        }
        if (++position < end) editor.Send(DeleteRange, position, end - position);
    }
    editor.Send(EndUndoAction);
}

struct LoadedDocument {
    std::string text;
    NativeByteRanges raw;
    FileEncoding encoding;
    bool bom;
    bool systemDefault;
    bool systemOem;
    OriginalFileVariables variables;
};

wxString DecodeLoadedDocument(LoadedDocument& loaded, const EncodingOptions& options,
                              std::optional<unsigned> nativeAnsiCodePage) {
    using enum FileEncoding;
    auto& bytes = loaded.text;
    const bool utf16 = loaded.encoding == Utf16Le || loaded.encoding == Utf16Be;
    if (!utf16) loaded.variables = ReadOriginalFileVariables(bytes, options.noFileVariables, options.noEncodingTags);
    if (loaded.bom) bytes.erase(0, utf16 ? 2 : 3);
    if (loaded.systemDefault && loaded.encoding != Utf8) {
        // Native ANSI keeps arbitrary bytes and aliases that cannot round-trip
        // through Unicode, just like the native Base64 Decode boundary.
        bytes = NativeDefaultEncodingBuffer(bytes, &loaded.raw, nativeAnsiCodePage);
    } else if (loaded.encoding == Gbk) {
        const auto decoded = DecodeGbk(bytes);
        if (!decoded) return wxString(_("The file is not valid GBK (CP936). The document was not changed."));
        bytes = *decoded;
    } else if (loaded.encoding == Windows1252 || loaded.encoding == Dos437) {
        bytes = DecodeSingleByte(bytes, loaded.encoding);
    } else if (loaded.encoding == Gb18030 || loaded.encoding == Big5) {
        const auto decoded = DecodeChinese(bytes, loaded.encoding);
        if (!decoded) return wxString(_("The file could not be decoded with the selected Chinese encoding."));
        bytes = *decoded;
    } else if (utf16) {
        auto decoded = loaded.encoding == Utf16Be ? DecodeUtf16Be(bytes) : DecodeUtf16Le(bytes);
        if (!decoded)
            return wxString(loaded.encoding == Utf16Be ? _("The file is not valid UTF-16BE.")
                                                       : _("The file is not valid UTF-16LE."));
        bytes = std::move(*decoded);
        loaded.variables = ReadOriginalFileVariables(bytes, options.noFileVariables, options.noEncodingTags);
    } else if (!IsUtf8(bytes))
        return wxString(_("The file is not valid UTF-8. No other encoding was assumed."));
    return {};
}

// Preparing a candidate never changes the live document. The caller can repeat
// this read if the discard/save decision rewrites the selected source.
std::variant<LoadedDocument, wxString> ReadDocument(const wxFileName& path,
                                                    const DocumentLoad::SourceDecoding& decoding,
                                                    const EncodingOptions& options,
                                                    std::optional<EncodingChoice> weakEncoding) {
    if (!path.FileExists()) return wxString(_("Choose an existing file to open."));
    wxFile file(path.GetFullPath(), wxFile::read);
    if (!file.IsOpened()) return wxString(_("Could not open the file for reading."));
    const auto length = file.Length();
    if (length < 0) return wxString(_("Could not determine the file size."));
    LoadedDocument loaded;
    auto& bytes = loaded.text;
    bytes.assign(static_cast<size_t>(length), '\0');
    if (file.Read(bytes.data(), bytes.size()) != length || !file.Close())
        return wxString(_("Could not read the complete file."));
    std::optional<FileEncoding> encoding;
    const auto* sourceEncoding = std::get_if<EncodingChoice>(&decoding);
    if (sourceEncoding)
        encoding = sourceEncoding->encoding;
    else if (const auto* codec = std::get_if<FileEncoding>(&decoding))
        encoding = *codec;
    const auto* quick = std::get_if<QuickReloadEncoding>(&decoding);
    const auto detected = quick ? DetectQuickReloadEncoding(bytes, *quick, options)
                                : DetectEncoding(path.GetFullPath(), bytes, encoding, options, weakEncoding);
    if (!detected.encoding)
        return wxString::Format(
            _("The selected encoding (code page %u) is not migrated. The document was not changed."),
            *detected.unsupportedCodePage);
    loaded.encoding = *detected.encoding;
    loaded.bom = detected.bom;
    loaded.systemDefault = sourceEncoding ? sourceEncoding->systemDefault : detected.systemDefault;
    loaded.systemOem = sourceEncoding ? sourceEncoding->systemOem : detected.systemOem;
    const auto error = DecodeLoadedDocument(loaded, options, detected.nativeAnsiCodePage);
    if (!error.empty()) return error;
    return loaded;
}

enum class LoadDecisionTiming { AfterRead, BeforeRead };
struct LoadPlan {
    std::optional<wxString> source;
    DocumentLoad::SourceDecoding decoding;
    std::optional<EncodingChoice> weakEncoding;
    const std::function<bool()>* beforeReplace = nullptr;
    LoadDecisionTiming decisionTiming = LoadDecisionTiming::AfterRead;

    bool ConfirmReplacement() const { return !beforeReplace || !*beforeReplace || (*beforeReplace)(); }
};

// Resolve each command once, before a decision can change the live session.
struct PlanDocumentLoad {
    const DocumentSession& document;

    std::optional<wxString> CurrentPath() const {
        return document.Path().empty() ? std::nullopt : std::optional(document.Path());
    }
    LoadPlan operator()(const DocumentLoad::Open& intent) const {
        return {intent.source, intent.decoding, {}, &intent.beforeReplace};
    }
    LoadPlan operator()(DocumentLoad::Reload) const { return {CurrentPath(), {}, document.CurrentEncoding()}; }
    LoadPlan operator()(DocumentLoad::ReloadWithEncoding intent) const { return {CurrentPath(), intent.encoding, {}}; }
    LoadPlan operator()(const DocumentLoad::ReloadAsUtf8& intent) const {
        return {CurrentPath(), FileEncoding::Utf8, {}, &intent.beforeReplace, LoadDecisionTiming::BeforeRead};
    }
    LoadPlan operator()(const DocumentLoad::ReloadQuickEncoding& intent) const {
        return {CurrentPath(), intent.encoding, {}, &intent.beforeReplace, LoadDecisionTiming::BeforeRead};
    }
};

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
    using enum Scintilla::Message;
    const auto caret = editor.Send(GetCurrentPos);
    const auto anchor = editor.Send(GetAnchor);
    const auto line = editor.Send(LineFromPosition, caret);
    const auto column = editor.Send(GetColumn, caret, line);
    const auto visibleTop = editor.Send(GetFirstVisibleLine);
    const auto documentTop = editor.Send(DocLineFromVisible, visibleTop);
    return {caret, anchor, line, column, visibleTop, documentTop, editor.Send(GetXOffset), editor.Send(GetScrollWidth)};
}

void RestoreReloadView(EditorControl& editor, const ReloadView& view) {
    using enum Scintilla::Message;
    // ClearAll resets width tracking. Restore the current file's measured range
    // before its view, so native layout cannot clamp the saved offset before paint.
    editor.Send(SetScrollWidth, static_cast<uintptr_t>(view.scrollWidth));
    editor.Send(SetSel, view.anchor, view.caret);
    // Original FileLoad compares the saved one-based line against the restored
    // zero-based line, then uses EditJumpTo/EditSelectEx.
    constexpr intptr_t ReloadLineDisplacementTolerance = 5;
    const auto displacement = view.line + 1 - editor.Send(LineFromPosition, editor.Send(GetCurrentPos));
    if (displacement > ReloadLineDisplacementTolerance || displacement < -ReloadLineDisplacementTolerance) {
        const auto position = view.line >= editor.Send(GetLineCount) ? editor.Send(GetLength)
                                                                     : editor.Send(FindColumn, view.line, view.column);
        editor.JumpToPosition(position);
    } else {
        editor.Send(EnsureVisible, view.documentTop);
        editor.Send(LineScroll, 0, view.visibleTop - editor.Send(GetFirstVisibleLine));
        editor.Send(SetXOffset, view.xOffset);
    }
}
} // namespace

wxString DocumentSession::FileNameText(FileNamePart part) const {
    wxString text = path_.empty()                    ? _("Untitled")
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
    readOnlyFileFlag_ = false;
    readOnlyFileChangeError_.clear();
    if (path_.empty()) {
        // TRANSLATORS: IDM_FILE_READONLY_FILE unavailable for an unnamed document; no parameters.
        readOnlyFileChangeError_ = _("Save the document before changing file permissions.");
        return false;
    }
#ifdef __WXMSW__
    const auto attributes = GetFileAttributesW(path_.wc_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        // TRANSLATORS: IDM_FILE_READONLY_FILE metadata error; %s is the native Windows error.
        readOnlyFileChangeError_ = wxString::Format(_("Could not read file attributes: %s"), wxSysErrorMsg());
    else if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        // TRANSLATORS: IDM_FILE_READONLY_FILE rejects a directory target; no parameters.
        readOnlyFileChangeError_ = _("This command changes a file, not a directory.");
    else
        readOnlyFile_ = readOnlyFileFlag_ = (attributes & FILE_ATTRIBUTE_READONLY) != 0;
#else
    struct stat state{};
    if (stat(path_.fn_str(), &state) != 0)
        // TRANSLATORS: Linux IDM_FILE_READONLY_FILE metadata error; %s is the native filesystem error.
        readOnlyFileChangeError_ = wxString::Format(_("Could not read file permissions: %s"), wxSysErrorMsg());
    else if (!S_ISREG(state.st_mode))
        // TRANSLATORS: IDM_FILE_READONLY_FILE rejects a non-regular-file target; no parameters.
        readOnlyFileChangeError_ = _("This command changes a file, not a directory.");
    else {
        readOnlyFileFlag_ = (state.st_mode & S_IWUSR) == 0;
        readOnlyFile_ = state.st_uid == geteuid() ? readOnlyFileFlag_ : !wxFileName::IsFileWritable(path_);
        if (state.st_uid != geteuid())
            // TRANSLATORS: Linux IDM_FILE_READONLY_FILE ownership restriction; no parameters.
            readOnlyFileChangeError_ = _("Only the file owner can change the owner's write permission.");
    }
#endif
    return readOnlyFile_;
}

wxString DocumentSession::ToggleReadOnlyFile() {
    RefreshReadOnlyFile();
    wxString error = readOnlyFileChangeError_;
    if (error.empty()) {
#ifdef __WXMSW__
        const auto attributes = GetFileAttributesW(path_.wc_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            !SetFileAttributesW(path_.wc_str(), attributes ^ FILE_ATTRIBUTE_READONLY))
            // TRANSLATORS: IDM_FILE_READONLY_FILE update error; %s is the native Windows error.
            error = wxString::Format(_("Could not change file attributes: %s"), wxSysErrorMsg());
#else
        struct stat state{};
        if (stat(path_.fn_str(), &state) != 0)
            // TRANSLATORS: Linux IDM_FILE_READONLY_FILE metadata error; %s is the native filesystem error.
            error = wxString::Format(_("Could not read file permissions: %s"), wxSysErrorMsg());
        else if (state.st_uid != geteuid())
            // TRANSLATORS: Linux IDM_FILE_READONLY_FILE ownership restriction; no parameters.
            error = _("Only the file owner can change the owner's write permission.");
        else if (chmod(path_.fn_str(),
                       (state.st_mode & (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)) ^ S_IWUSR) != 0)
            // TRANSLATORS: Linux IDM_FILE_READONLY_FILE update error; %s is the native filesystem error.
            error = wxString::Format(_("Could not change the file owner's write permission: %s"), wxSysErrorMsg());
#endif
    }
    RefreshReadOnlyFile();
    // Original IDM_FILE_READONLY_FILE releases F10 when the disk file becomes writable.
    if (!readOnlyFile_ && readOnlyFileChangeError_.empty()) editor_.Send(Scintilla::Message::SetReadOnly, 0);
    return error;
}

SaveResult DocumentSession::SaveFirst(const std::optional<wxString>& destination,
                                      const EncodingLossDecision& decideLoss, SaveFileType fileType,
                                      SaveTimeMode timeMode) {
    using enum SaveStatus;
    if (!destination) return {Cancelled, {}};
    if (!path_.empty()) return {Failed, _("This document already has a file name.")};
    if (destination->empty()) return {Failed, _("Choose a file name before saving.")};
    return SaveTo(*destination, decideLoss, fileType, false, timeMode);
}

SaveResult DocumentSession::Save(const EncodingLossDecision& decideLoss, SaveTimeMode timeMode) {
    if (path_.empty()) return {SaveStatus::Failed, _("Choose a file name before saving.")};
    return SaveTo(path_, decideLoss, std::nullopt, false, timeMode);
}

SaveResult DocumentSession::SaveAs(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss,
                                   SaveFileType fileType, SaveTimeMode timeMode) {
    if (!destination) return {SaveStatus::Cancelled, {}};
    if (destination->empty()) return {SaveStatus::Failed, _("Choose a file name before saving.")};
    return SaveTo(*destination, decideLoss, fileType, false, timeMode);
}

SaveResult DocumentSession::SaveCopy(const std::optional<wxString>& destination,
                                     const EncodingLossDecision& decideLoss) {
    if (!destination) return {SaveStatus::Cancelled, {}};
    if (destination->empty()) return {SaveStatus::Failed, _("Choose a file name before saving.")};
    return SaveTo(*destination, decideLoss, std::nullopt, true);
}

DocumentSession::~DocumentSession() {
    StopAutoSave(true);
}

wxString DocumentSession::BackupDirectory() {
    return DefaultBackupDirectory();
}

void DocumentSession::StartBackupTimer(bool reset) {
    // AutoSave_Start at Notepad4 89b8b36: repeat from the first edit; later
    // edits do not reset the timer. Win32 SetTimer caps at USER_TIMER_MAXIMUM.
    if ((autoSave_.flags & AutoSaveOption::Periodic) && autoSave_.periodMilliseconds) {
        if (reset || !backupTimer_.IsRunning())
            backupTimer_.Start(static_cast<int>(
                std::min(autoSave_.periodMilliseconds, static_cast<uint32_t>(std::numeric_limits<int>::max()))));
    } else {
        backupTimer_.Stop();
    }
}

void DocumentSession::ConfigureAutoSave(AutoSaveOptions options) {
    const bool reset = options.periodMilliseconds != autoSave_.periodMilliseconds;
    autoSave_ = options;
    if (!(options.flags & AutoSaveOption::Periodic))
        StopAutoSave(false);
    else
        StartBackupTimer(reset);
}

void DocumentSession::DocumentChanged() {
    const auto revision = editor_.DocumentRevision();
    if (revision == observedBackupRevision_) return;
    observedBackupRevision_ = revision;
    StartBackupTimer(false);
}

void DocumentSession::StopAutoSave(bool keepBackups) {
    // AutoSave_Stop: release this lifecycle's paths, retaining files only when
    // requested by the file action or the original manual-deletion option.
    backupTimer_.Stop();
    observedBackupRevision_ = lastBackupRevision_ = editor_.DocumentRevision();
    if (!keepBackups && !(autoSave_.flags & AutoSaveOption::ManuallyDelete)) {
        const wxLogNull quiet;
        for (const auto& path : automaticBackupPaths_)
            wxRemoveFile(path);
    }
    automaticBackupPaths_.clear();
}

void DocumentSession::SaveBackup(bool automatic) {
    const auto revision = editor_.DocumentRevision();
    if (automatic && (!IsModified() || revision == lastBackupRevision_)) return;
    const auto path = WriteBackup(automatic);
    if (path.empty()) return;
    lastBackupRevision_ = revision;
    if (!automatic) return; // Manual Save Backup is never rotated or cleaned up.
    if (automaticBackupPaths_.size() == AutoSaveOption::MaximumManagedBackups) {
        if (!(autoSave_.flags & AutoSaveOption::ManuallyDelete)) {
            const wxLogNull quiet;
            wxRemoveFile(automaticBackupPaths_.front());
        }
        automaticBackupPaths_.erase(automaticBackupPaths_.begin());
    }
    automaticBackupPaths_.push_back(path);
}

wxString DocumentSession::WriteBackup(bool automatic) {
    using enum Scintilla::Message;
    const auto length = static_cast<size_t>(editor_.Send(GetLength));
    if (!length) return {};
    std::string bytes(length + 1, '\0');
    editor_.Send(GetText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    bytes.resize(length);
    // In Notepad4 an ANSI document is held in its native code page. Recover
    // that buffer using the editor's preserved native bytes and current text.
    if (systemDefaultEncoding_) bytes = NativeBytesFromRanges(bytes, 0, *editor_.RawNativeBytes());
    const wxLogNull quiet;
    const bool untitled = path_.empty();
    const auto sibling = BackupPath(untitled ? wxString("Untitled") : path_, automatic);
    if (!untitled) {
        const auto result = CreateBackup(sibling, bytes);
        if (result != BackupCreation::OpenFailed) return result == BackupCreation::Complete ? sibling : wxString{};
    }

    const auto directory = DefaultBackupDirectory();
    if (!wxDirExists(directory)) wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const auto backup = wxFileName(directory, wxFileName(sibling).GetFullName()).GetFullPath();
    if (untitled) return CreateBackup(backup, bytes) == BackupCreation::Complete ? backup : wxString{};
    auto metadata = ("AutoSave for " + path_).utf8_string();
    auto output = systemDefaultEncoding_ ? NativeDefaultEncodingBytes(metadata) : std::move(metadata);
    output += BackupMetadataEnding(static_cast<Scintilla::EndOfLine>(editor_.Send(GetEOLMode)));
    output += bytes;
    return CreateBackup(backup, output) == BackupCreation::Complete ? backup : wxString{};
}

SaveResult DocumentSession::SaveTo(const wxString& destination, const EncodingLossDecision& decideLoss,
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
    const auto raw = editor_.RawNativeBytes();
    const auto encoded = EncodeForSave(std::move(bytes), encoding_, hasBom_, systemDefaultEncoding_, *raw, decideLoss);
    if (const auto* result = std::get_if<SaveResult>(&encoded)) return *result;
    const auto error = WriteSafely(destination, std::get<std::string>(encoded), temporary);
    if (!error.empty()) return {SaveStatus::Failed, error};
    if (originalTimes) SetFileTimes(destination, *originalTimes);
    // Original EditSaveFile/FileSave leave the live identity, scheme and save
    // point alone for Save Copy; optional cleanup above remains in its history.
    if (copy) {
        StopAutoSave(false);
        return {SaveStatus::Saved, {}};
    }
    CommitSaveState(destination, fileType);
    StopAutoSave(false);
    return {SaveStatus::Saved, {}};
}

void DocumentSession::CommitSaveState(const wxString& destination, std::optional<SaveFileType> fileType) {
    const auto previousLanguage = Language();
    const auto previousDialect = EffectiveLanguage();
    if (fileType) {
        const auto* selected = std::get_if<SyntaxLanguage>(&*fileType);
        if (selected) {
            languageOverride_ = *selected;
            // Original Save As keeps np2LexLangIndex when its selected lexer
            // equals pLexCurrent (Notepad4.cpp::FileSave).
            if (*selected != previousLanguage) markupDialect_.reset();
        } else if (std::get<SaveType>(*fileType) == SaveType::CurrentScheme)
            languageOverride_ = previousLanguage;
        else {
            languageOverride_.reset();
            markupDialect_.reset();
        }
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
    if (previousLanguage != Language() || previousDialect != EffectiveLanguage()) {
        ApplyEmptySchemeDefaults(false);
        ApplySyntaxLanguage(editor_, Language(), markupDialect_);
    }
}

bool DocumentSession::UpdateFileTime() {
    if (path_.empty()) return false;
#ifdef __WXMSW__
    // Opening the update handle can expose a newer access time than the
    // pre-operation path attributes. Preserve that observed value separately.
    const auto originalAccess = ReadAccessTime(path_);
    const auto file = CreateFileW(path_.wc_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    FILE_BASIC_INFO times{};
    bool updated = GetFileInformationByHandleEx(file, FileBasicInfo, &times, sizeof(times)) != 0;
    if (updated) {
        if (originalAccess) {
            times.LastAccessTime.LowPart = originalAccess->dwLowDateTime;
            times.LastAccessTime.HighPart = static_cast<LONG>(originalAccess->dwHighDateTime);
        }
        GetSystemTimePreciseAsFileTime(reinterpret_cast<FILETIME*>(&times.LastWriteTime));
        updated = SetFileInformationByHandle(file, FileBasicInfo, &times, sizeof(times)) != 0;
    }
    CloseHandle(file);
#else
    const int file = ::open(path_.utf8_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (file < 0) return false;
    // Preserve access time; ctime follows the filesystem's metadata-update rules.
    const std::array<timespec, 2> times{{{0, UTIME_OMIT}, {0, UTIME_NOW}}};
    const bool updated = ::futimens(file, times.data()) == 0;
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

OpenResult DocumentSession::Load(const DocumentLoad::Intent& intent,
                                 const std::function<void()>& configureLoadedDocument) {
    const auto plan = std::visit(PlanDocumentLoad{*this}, intent);
    if (!plan.source) return {OpenStatus::Cancelled, {}};
    // Notepad4 FileLoad compares the supplied spelling before canonicalizing the
    // load path. Do not turn distinct spellings or file-system aliases into reloads.
#ifdef __WXMSW__
    const bool samePath =
        !path_.empty() && CompareStringOrdinal(plan.source->wc_str(), -1, path_.wc_str(), -1, TRUE) == CSTR_EQUAL;
#else
    const bool samePath = !path_.empty() && *plan.source == path_;
#endif
    const auto view = CaptureReloadView(editor_);
    const bool keepReadOnly = samePath && editor_.Send(Scintilla::Message::GetReadOnly);
    wxFileName loadPath(*plan.source);
    loadPath.Normalize(wxPATH_NORM_ENV_VARS | wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG |
                       wxPATH_NORM_SHORTCUT);
    wxLogNull quiet;
    const bool wasModified = IsModified();
    if (plan.decisionTiming == LoadDecisionTiming::BeforeRead && !plan.ConfirmReplacement())
        return {OpenStatus::Cancelled, {}};
    auto candidate = ReadDocument(loadPath, plan.decoding, encodingOptions_, plan.weakEncoding);
    if (const auto* error = std::get_if<wxString>(&candidate)) return {OpenStatus::Failed, *error};
    if (plan.decisionTiming == LoadDecisionTiming::AfterRead && !plan.ConfirmReplacement())
        return {OpenStatus::Cancelled, {}};
    // Save may have rewritten the selected source (including a newly chosen
    // first-save destination). Read it again after that explicit Save succeeds.
    if (plan.decisionTiming == LoadDecisionTiming::AfterRead && wasModified && !IsModified()) {
        // Release the old decoded text before allocating another whole-file read.
        candidate.emplace<wxString>();
        candidate = ReadDocument(loadPath, plan.decoding, encodingOptions_, plan.weakEncoding);
        if (const auto* error = std::get_if<wxString>(&candidate)) return {OpenStatus::Failed, *error};
    }
    auto& loaded = std::get<LoadedDocument>(candidate);
    const auto result = Replace(loaded.text, loadPath.GetFullPath(),
                                EncodingChoice(loaded.encoding, loaded.bom, loaded.systemDefault, loaded.systemOem),
                                samePath, std::move(loaded.raw));
    originalEncoding_ = CurrentEncoding();
    fileVariables_ = std::move(loaded.variables);
    // FileLoad applies the loaded file's indentation before restoring its view
    // or resolving a saved visual column against the new tab width.
    if (configureLoadedDocument) configureLoadedDocument();
    if (keepReadOnly || IsReadOnlyFile()) editor_.Send(Scintilla::Message::SetReadOnly, 1);
    if (samePath) RestoreReloadView(editor_, view);
    StopAutoSave(!samePath);
    return result;
}

OpenResult DocumentSession::Replace(const std::string& bytes, const wxString& path, EncodingChoice encoding,
                                    bool keepLanguage, NativeByteRanges raw) {
    using enum Scintilla::Message;
    const auto lineEndings = CountLineEndings(bytes);
    editor_.Send(SetReadOnly, 0);
    const auto changeHistory = editor_.Send(GetChangeHistory);
    editor_.Send(SetChangeHistory, 0);
    const auto selectionHistory = editor_.Send(GetUndoSelectionHistory);
    editor_.Send(SetUndoSelectionHistory, 0);
    editor_.Send(SetUndoCollection, 0);
    editor_.Send(ClearAll);
    editor_.Send(MarkerDeleteAll, EditorMarker::Bookmark);
    if (raw.empty())
        editor_.Send(AddText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    else {
        editor_.Send(SetTargetStart, 0);
        editor_.Send(SetTargetEnd, 0);
        editor_.ReplaceTargetWithNativeBytes(bytes, std::move(raw));
    }
    editor_.Send(SetEOLMode, static_cast<uintptr_t>(std::to_underlying(SelectEol(lineEndings, defaultEolMode_))));
    editor_.Send(SetUndoCollection, 1);
    editor_.Send(EmptyUndoBuffer);
    editor_.Send(GotoPos, 0);
    editor_.Send(SetXOffset, 0);
    editor_.Send(SetSavePoint);
    editor_.Send(SetChangeHistory, changeHistory);
    editor_.Send(SetUndoSelectionHistory, selectionHistory);
    path_ = path;
    RefreshReadOnlyFile();
    encoding_ = encoding.encoding;
    hasBom_ = encoding.bom;
    systemDefaultEncoding_ = encoding.systemDefault;
    systemOemEncoding_ = encoding.systemOem;
    bool unknownLanguage = false;
    if (!keepLanguage) {
        languageOverride_.reset();
        markupDialect_.reset();
        unknownLanguage = !DetectAutomaticLanguage();
        ApplyEmptySchemeDefaults(false);
        ApplySyntaxLanguage(editor_, Language(), markupDialect_);
    }
    return {OpenStatus::Opened, {}, lineEndings, unknownLanguage};
}

void DocumentSession::New() {
    const auto encoding =
        DefaultEncodingChoice(encodingOptions_.defaultEncoding).value_or(EncodingChoice(FileEncoding::Utf8));
    Replace({}, {}, encoding);
    StopAutoSave(true);
    originalEncoding_ = CurrentEncoding();
    fileVariables_ = {};
}

bool DocumentSession::IsModified() const {
    return editor_.Send(Scintilla::Message::GetModify) || originalEncoding_ != CurrentEncoding();
}

bool DocumentSession::SelectEncoding(EncodingChoice choice, const EncodingChangeDecision& decide) {
    using enum Scintilla::Message;
    // Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
    if (choice == CurrentEncoding()) return false;
    if (decide && choice.systemDefault != systemDefaultEncoding_) {
        const auto length = editor_.Send(GetLength);
        if ((length || editor_.Send(GetUndoActions)) &&
            !decide(length ? EncodingChangeWarning::AnsiConversion : EncodingChangeWarning::EmptyAnsiHistory))
            return false;
    }
    // Confirmation can run a modal event loop. Apply the choice to the live
    // document, including changes to its encoding, text or history while waiting.
    if (choice == CurrentEncoding()) return false;
    const bool crossesAnsi = choice.systemDefault != systemDefaultEncoding_;
    const auto length = editor_.Send(GetLength);
    // Pinned EditConvertText changes the native buffer, discarding its history.
    // This adapter keeps the original core in UTF-8 and round-trips native text
    // at that boundary. UTF-8 native locales need no buffer conversion, as in
    // the original's cpSource == cpDest early return.
    const auto native = NativeEncodingChoice();
    if (crossesAnsi && native && native->encoding != FileEncoding::Utf8 && length < NativeConversionLimit) {
        std::string text(static_cast<size_t>(length) + 1, '\0');
        editor_.Send(GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
        text.resize(static_cast<size_t>(length));
        if (choice.systemDefault)
            text = NativeDefaultEncodingText(wxString::FromUTF8(text.data(), text.size())).utf8_string();
        else if (const auto raw = editor_.RawNativeBytes(); !raw->empty())
            text = NativeDefaultEncodingUnicode(NativeBytesFromRanges(text, 0, *raw));
        const auto history = editor_.Send(GetChangeHistory);
        const auto selectionHistory = editor_.Send(GetUndoSelectionHistory);
        editor_.Send(SetReadOnly, 0);
        editor_.Send(Cancel);
        editor_.Send(SetChangeHistory, 0);
        editor_.Send(SetUndoSelectionHistory, 0);
        editor_.Send(SetUndoCollection, 0);
        editor_.Send(EmptyUndoBuffer);
        editor_.Send(ClearAll);
        editor_.Send(MarkerDeleteAll, static_cast<uintptr_t>(-1));
        editor_.Send(AppendText, text.size(), reinterpret_cast<intptr_t>(text.data()));
        editor_.Send(EmptyUndoBuffer);
        editor_.Send(SetUndoCollection, 1);
        if (!length && path_.empty()) editor_.Send(SetSavePoint);
        editor_.Send(SetChangeHistory, history);
        editor_.Send(SetUndoSelectionHistory, selectionHistory);
    }
    // Notepad4 89b8b36 Notepad4.cpp IDM_ENCODING_SELECT: an empty
    // Untitled document adopts the format; a named one compares BOM presence.
    // The original core continues to own text, selection and Undo/Redo.
    if (editor_.Send(GetLength) == 0 && (path_.empty() || choice.bom == (originalEncoding_ && originalEncoding_->bom)))
        originalEncoding_ = choice;
    if (length && crossesAnsi) originalEncoding_.reset();
    encoding_ = choice.encoding;
    hasBom_ = choice.bom;
    systemDefaultEncoding_ = choice.systemDefault;
    systemOemEncoding_ = choice.systemOem;
    return true;
}

void DocumentSession::SelectLanguage(std::optional<SyntaxLanguage> language) {
    const auto previous = Language();
    const auto previousDialect = EffectiveLanguage();
    markupDialect_.reset();
    // Pinned Styles.cpp::Style_SelectLexer sniffs only the canonical Web/XML
    // choices; explicit dialect choices retain the user's selected language.
    if (language == SyntaxLanguage::Html || language == SyntaxLanguage::Xml) {
        std::array<char, SyntaxDetectionLimits::DocTypeBytes + 1> prefix{};
        editor_.Send(Scintilla::Message::GetText, prefix.size() - 1, reinterpret_cast<intptr_t>(prefix.data()));
        markupDialect_ = DetectMarkupDocType(prefix.data());
    }
    languageOverride_ = language;
    if (!language) DetectAutomaticLanguage();
    if (previous != Language() || previousDialect != EffectiveLanguage()) {
        ApplyEmptySchemeDefaults(SyntaxSettingsLanguage(previous) != SyntaxSettingsLanguage(Language()));
        ApplySyntaxLanguage(editor_, Language(), markupDialect_);
    }
}

void DocumentSession::ApplyEmptySchemeDefaults(bool overrideEncoding) {
    // Pinned Styles.cpp::Style_SetLexer / Notepad4.cpp::EditApplyDefaultEncoding.
    // Undo action count includes pending Redo; clean-but-edited empties retain
    // their current encoding/EOL. Scheme defaults preserve an existing BOM.
    using Scintilla::Message;
    if (editor_.Send(Message::GetLength) || editor_.Send(Message::GetUndoActions)) return;
    const bool shell = SyntaxSettingsLanguage(Language()) == SyntaxLanguage::Bash;
    editor_.Send(Message::SetEOLMode,
                 static_cast<uintptr_t>(std::to_underlying(shell ? Scintilla::EndOfLine::Lf : defaultEolMode_)));
    // Style_SetLexer's LexerChanged_Override bit is set by a user switch to a
    // different scheme, but not by FileLoad/New or automatic save detection.
    if (hasBom_) return;
    std::optional<EncodingChoice> encoding = CurrentEncoding();
    if (shell || overrideEncoding)
        encoding = shell || encodingOptions_.loadASCIIasUTF8 ? std::optional(EncodingChoice(FileEncoding::Utf8))
                                                             : DefaultEncodingChoice(encodingOptions_.defaultEncoding);
    // EditApplyDefaultEncoding resolves CPI_DEFAULT through Encoding_GetAnsiIndex
    // even for New/FileLoad. Native UTF-8 is always explicit; either load flag
    // also promotes a supported legacy ACP to its explicit catalog identity.
    if (encoding && encoding->systemDefault &&
        (encoding->encoding == FileEncoding::Utf8 || encodingOptions_.loadANSIasUTF8 ||
         encodingOptions_.loadASCIIasUTF8))
        encoding = EncodingChoice(encoding->encoding);
    if (encoding && *encoding != CurrentEncoding()) {
        encoding_ = encoding->encoding;
        hasBom_ = encoding->bom;
        systemDefaultEncoding_ = encoding->systemDefault;
        systemOemEncoding_ = encoding->systemOem;
        originalEncoding_ = CurrentEncoding();
    }
}

bool DocumentSession::DetectAutomaticLanguage() {
    // The pinned core's GetText count excludes its terminating NUL.
    // Match original SciCall_GetText(255) with the original 256-byte buffer.
    // Style_GetDocTypeLanguage inspects a larger header than the 255-byte
    // filename/shebang/markup guess. DetectLanguage retains both limits.
    std::array<char, SyntaxDetectionLimits::DocTypeBytes + 1> prefix{};
    editor_.Send(Scintilla::Message::GetText, prefix.size() - 1, reinterpret_cast<intptr_t>(prefix.data()));
    const auto detected = DetectLanguage(path_, prefix.data(), detection_);
    automaticLanguage_ = detected ? detected->schemeLanguage : SyntaxLanguage::PlainText;
    markupDialect_ = detected ? detected->markupDialect : std::nullopt;
    return detected.has_value();
}
