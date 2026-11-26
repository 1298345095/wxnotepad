#pragma once

#include <wx/string.h>
#include <wx/timer.h>
#include "settings/auto_save_options.h"
#include <cstdint>
#include "ScintillaTypes.h"
#include "syntax/syntax_detection.h"
#include "document/encoding/encoding_detection.h"
#include "document/file_variables.h"
#include "editor/native_byte_history.h"
#include <optional>
#include <functional>
#include <string>
#include <variant>
#include <vector>

class EditorControl;
enum class EncodingLossChoice { Cancel, Replace };
using EncodingLossDecision = std::function<EncodingLossChoice()>;
enum class EncodingChangeWarning { AnsiConversion, EmptyAnsiHistory };
using EncodingChangeDecision = std::function<bool(EncodingChangeWarning)>;
// The dialog adapter translates its native filter index into this semantic choice.
enum class SaveType : uint8_t { CurrentScheme, AllFiles };
using SaveFileType = std::variant<SaveType, SyntaxLanguage>;
struct SaveCleanupOptions {
    bool fixLineEndings = false;
    bool fixTrailingBlanks = false;
};

enum class SaveStatus { Saved, Cancelled, Failed };
enum class SaveTimeMode { Current, Original };
enum class FileNamePart { BaseName, FileName, FullPath };
struct SaveResult {
    SaveStatus status;
    wxString error;
};
enum class OpenStatus { Opened, Cancelled, Failed };
namespace DocumentLoad {
// The public ANSI source alias follows the original legacy recode decision;
// it is distinct from selecting the editor's native current format.
struct NativeAnsiSource {};
// Automatic detection, an explicit codec or native identity, or a quick
// command's source. Detection supplies the loaded document's BOM.
using SourceDecoding =
    std::variant<std::monostate, FileEncoding, EncodingChoice, QuickReloadEncoding, NativeAnsiSource>;
struct Open {
    std::optional<wxString> source;
    SourceDecoding decoding{};
    // Asked after a successful read. Omission means replacement is already approved.
    std::function<bool()> beforeReplace{};
};
struct Reload {};
struct ReloadWithEncoding {
    FileEncoding encoding;
};
struct ReloadAsUtf8 {
    // Asked before reading, after the current path and view have been captured.
    std::function<bool()> beforeReplace{};
};
struct ReloadQuickEncoding {
    QuickReloadEncoding encoding;
    std::function<bool()> beforeReplace{};
};
using Intent = std::variant<Open, Reload, ReloadWithEncoding, ReloadAsUtf8, ReloadQuickEncoding>;
} // namespace DocumentLoad
struct LineEndingCounts {
    size_t crlf = 0;
    size_t cr = 0;
    size_t lf = 0;
    bool Mixed() const { return (crlf != 0 && (cr != 0 || lf != 0)) || (cr != 0 && lf != 0); }
};
struct OpenResult {
    OpenStatus status;
    wxString error;
    LineEndingCounts lineEndings;
    bool unknownLanguage = false;
};

// File identity belongs to the session; the original core owns text and history.
class DocumentSession final {
public:
    explicit DocumentSession(EditorControl& editor, EncodingOptions options = {}, SyntaxDetectionOptions detection = {},
                             AutoSaveOptions autoSave = {})
        : autoSave_(autoSave), editor_(editor), encodingOptions_(options), detection_(detection) {}
    ~DocumentSession();
    const wxString& Path() const { return path_; }
    wxString FileNameText(FileNamePart part) const;
    bool IsReadOnlyFile() const { return readOnlyFile_; }
    // Native read-only attribute / owner-write bit; non-owner Save protection
    // still follows effective access instead of another user's owner bit.
    bool ReadOnlyFileFlag() const { return readOnlyFileFlag_; }
    bool RefreshReadOnlyFile();
    const wxString& ReadOnlyFileChangeError() const { return readOnlyFileChangeError_; }
    wxString ToggleReadOnlyFile();
    // A chosen path represents the caller's approved destination; no path is Cancel.
    SaveResult SaveFirst(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss = {},
                         SaveFileType fileType = SaveType::CurrentScheme,
                         SaveTimeMode timeMode = SaveTimeMode::Current);
    SaveResult Save(const EncodingLossDecision& decideLoss = {}, SaveTimeMode timeMode = SaveTimeMode::Current);
    SaveResult SaveAs(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss = {},
                      SaveFileType fileType = SaveType::CurrentScheme, SaveTimeMode timeMode = SaveTimeMode::Current);
    SaveResult SaveCopy(const std::optional<wxString>& destination, const EncodingLossDecision& decideLoss = {});
    void SaveBackup(bool automatic = false);
    void ConfigureAutoSave(AutoSaveOptions options);
    void DocumentChanged();
    void StopAutoSave(bool keepBackups);
    static wxString BackupDirectory();
    bool UpdateFileTime();
    // Reload intents obtain their path and any current-encoding hint from this
    // session. Loaded settings are applied before restoring the same-path view.
    OpenResult Load(const DocumentLoad::Intent& intent, const std::function<void()>& configureLoadedDocument = {});
    void New();
    void SetEncodingOptions(EncodingOptions options) { encodingOptions_ = options; }
    SaveCleanupOptions SaveCleanup() const { return saveCleanup_; }
    void SetSaveCleanup(SaveCleanupOptions options) { saveCleanup_ = options; }
    void SetDefaultEolMode(Scintilla::EndOfLine mode) { defaultEolMode_ = mode; }
    bool HasUtf8Bom() const { return encoding_ == FileEncoding::Utf8 && hasBom_; }
    FileEncoding Encoding() const { return encoding_; }
    bool HasBom() const { return hasBom_; }
    bool IsDefaultEncoding() const { return systemDefaultEncoding_; }
    bool IsOemEncoding() const { return systemOemEncoding_; }
    EncodingChoice CurrentEncoding() const {
        return EncodingChoice(encoding_, hasBom_, systemDefaultEncoding_, systemOemEncoding_);
    }
    bool IsModified() const;
    // An omitted decision retains noninteractive selection. At most one decision
    // is requested; acceptance is followed by a fresh state read.
    bool SelectEncoding(EncodingChoice choice, const EncodingChangeDecision& decide = {});
    const OriginalFileVariables& FileVariables() const { return fileVariables_; }
    // The selected scheme supplies styles, settings and file-filter identity.
    SyntaxLanguage Language() const { return languageOverride_.value_or(automaticLanguage_); }
    // Original status/menu dialect may differ after a Web/XML header sniff.
    SyntaxLanguage EffectiveLanguage() const { return markupDialect_.value_or(Language()); }
    std::optional<SyntaxLanguage> LanguageOverride() const {
        return languageOverride_ ? std::optional(EffectiveLanguage()) : std::nullopt;
    }
    void SelectLanguage(std::optional<SyntaxLanguage> language);
    void SetSyntaxDetectionOptions(SyntaxDetectionOptions options) { detection_ = options; }

private:
    class BackupTimer final : public wxTimer {
    public:
        explicit BackupTimer(DocumentSession& document) : document_(document) {}
        void Notify() override { document_.SaveBackup(true); }

    private:
        DocumentSession& document_;
    };
    BackupTimer backupTimer_{*this};
    AutoSaveOptions autoSave_;
    uint64_t lastBackupRevision_ = 0;
    uint64_t observedBackupRevision_ = 0;
    std::vector<wxString> automaticBackupPaths_;
    void StartBackupTimer(bool reset);
    wxString WriteBackup(bool automatic);
    EditorControl& editor_;
    wxString path_;
    FileEncoding encoding_ = FileEncoding::Utf8;
    bool hasBom_ = false;
    bool readOnlyFile_ = false;
    bool readOnlyFileFlag_ = false;
    wxString readOnlyFileChangeError_;
    SaveCleanupOptions saveCleanup_;
    Scintilla::EndOfLine defaultEolMode_ = Scintilla::EndOfLine::CrLf;
    EncodingOptions encodingOptions_;
    bool systemDefaultEncoding_ = false;
    bool systemOemEncoding_ = false;
    std::optional<EncodingChoice> originalEncoding_ = EncodingChoice(FileEncoding::Utf8);
    OriginalFileVariables fileVariables_;
    std::optional<SyntaxLanguage> languageOverride_;
    // Original pLexCurrent selects settings/styles/filter; np2LexLangIndex can
    // independently name a document-type dialect for canonical Web/XML choices.
    std::optional<SyntaxLanguage> markupDialect_;
    SyntaxDetectionOptions detection_;
    SyntaxLanguage automaticLanguage_ = SyntaxLanguage::PlainText;
    bool DetectAutomaticLanguage();
    void ApplyEmptySchemeDefaults(bool overrideEncoding);
    void ApplySaveCleanup();
    void CommitSaveState(const wxString& destination, std::optional<SaveFileType> fileType);
    SaveResult SaveTo(const wxString& destination, const EncodingLossDecision& decideLoss,
                      std::optional<SaveFileType> fileType = std::nullopt, bool copy = false,
                      SaveTimeMode timeMode = SaveTimeMode::Current);
    OpenResult Replace(const std::string& bytes, const wxString& path, EncodingChoice encoding,
                       bool keepLanguage = false, NativeByteRanges raw = {});
};
