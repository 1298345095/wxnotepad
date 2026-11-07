#pragma once

#include <wx/string.h>
#include <cstdint>
#include "ScintillaTypes.h"
#include "syntax/syntax_catalog.h"
#include "document/encoding/encoding_detection.h"
#include "document/file_variables.h"
#include <optional>
#include <functional>
#include <string>
#include <variant>

class EditorControl;
enum class GbkLossChoice { Cancel, Replace };
using GbkLossDecision = std::function<GbkLossChoice()>;
// The dialog adapter translates its native filter index into this semantic choice.
enum class SaveType : uint8_t { CurrentScheme, AllFiles };
using SaveFileType = std::variant<SaveType, SyntaxLanguage>;
struct SaveCleanupOptions {
    bool fixLineEndings = false;
    bool fixTrailingBlanks = false;
};

enum class SaveStatus { Saved, Cancelled, Failed };
struct SaveResult {
    SaveStatus status;
    wxString error;
};
enum class OpenStatus { Opened, Cancelled, Failed };
struct LineEndingCounts {
    size_t crlf = 0;
    size_t cr = 0;
    size_t lf = 0;
    bool Mixed() const { return (crlf != 0) + (cr != 0) + (lf != 0) > 1; }
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
    explicit DocumentSession(EditorControl& editor, EncodingOptions options = {}, SyntaxDetectionOptions detection = {})
        : editor_(editor), encodingOptions_(options), detection_(detection) {}
    const wxString& Path() const { return path_; }
    bool IsReadOnlyFile() const { return readOnlyFile_; }
    bool RefreshReadOnlyFile();
    // A chosen path represents the caller's approved destination; no path is Cancel.
    SaveResult SaveFirst(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss = {},
                         SaveFileType fileType = SaveType::CurrentScheme);
    SaveResult Save(const GbkLossDecision& decideLoss = {});
    SaveResult SaveAs(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss = {},
                      SaveFileType fileType = SaveType::CurrentScheme);
    OpenResult Open(const std::optional<wxString>& source, const std::function<bool()>& beforeReplace,
                    std::optional<FileEncoding> encoding = std::nullopt,
                    std::optional<EncodingChoice> weakEncoding = std::nullopt,
                    const std::function<void()>& configureLoadedDocument = {});
    void New();
    SaveCleanupOptions SaveCleanup() const { return saveCleanup_; }
    void SetSaveCleanup(SaveCleanupOptions options) { saveCleanup_ = options; }
    void SetDefaultEolMode(Scintilla::EndOfLine mode) { defaultEolMode_ = mode; }
    bool HasUtf8Bom() const { return encoding_ == FileEncoding::Utf8 && hasBom_; }
    FileEncoding Encoding() const { return encoding_; }
    bool HasBom() const { return hasBom_; }
    bool IsDefaultEncoding() const { return systemDefaultEncoding_; }
    const OriginalFileVariables& FileVariables() const { return fileVariables_; }
    SyntaxLanguage Language() const { return languageOverride_.value_or(automaticLanguage_); }
    std::optional<SyntaxLanguage> LanguageOverride() const { return languageOverride_; }
    void SelectLanguage(std::optional<SyntaxLanguage> language);

private:
    EditorControl& editor_;
    wxString path_;
    FileEncoding encoding_ = FileEncoding::Utf8;
    bool hasBom_ = false;
    bool readOnlyFile_ = false;
    SaveCleanupOptions saveCleanup_;
    Scintilla::EndOfLine defaultEolMode_ = Scintilla::EndOfLine::CrLf;
    EncodingOptions encodingOptions_;
    bool systemDefaultEncoding_ = false;
    OriginalFileVariables fileVariables_;
    std::optional<SyntaxLanguage> languageOverride_;
    SyntaxDetectionOptions detection_;
    SyntaxLanguage automaticLanguage_ = SyntaxLanguage::PlainText;
    bool DetectAutomaticLanguage();
    void ApplySaveCleanup();
    SaveResult SaveTo(const wxString& destination, const GbkLossDecision& decideLoss,
                      std::optional<SaveFileType> fileType = std::nullopt);
    OpenResult Replace(const std::string& bytes, const wxString& path, FileEncoding encoding, bool bom,
                       bool keepLanguage = false);
};
