#pragma once

#include <wx/string.h>
#include <cstdint>
#include <span>
#include <string_view>
#include <optional>

enum class SyntaxLanguage : uint8_t {
    PlainText,
    Cpp,
    Python,
    Json,
    JavaScript,
    Jsx,
    TypeScript,
    Tsx,
    Bash,
    CShell,
    M4,
    Html,
    AspVb,
    AspJs,
    AspxCs,
    AspxVb,
    Jsp,
    Jspx,
    Xml,
    Svg,
    Xsd,
    Xslt,
    Dtd,
    Plist,
    Rust,
    Go,
    Lua,
    Toml,
    Yaml,
    CMake,
    Count
};
struct SyntaxDetectionOptions {
    bool autoSelect = true;
    bool noCgiGuess = false;
    bool noHtmlGuess = false;
};
struct DetectedSyntax {
    // Original pLex is chosen before the independent np2LexLangIndex sniff.
    SyntaxLanguage schemeLanguage;
    std::optional<SyntaxLanguage> markupDialect = std::nullopt;
};
struct SyntaxStyle {
    int id;
    unsigned int foreground;
    unsigned int background;
    bool bold;
    bool italic = false;
    bool eolFilled = false;
};
struct SyntaxKeywords {
    const char* text;
    uintptr_t attributes;
};
// Original KeywordAttr values in SCI_SETKEYWORDS' attribute byte.
namespace SyntaxKeywordAttribute {
inline constexpr uintptr_t MakeLower = 1U << 8U;
inline constexpr uintptr_t PreSorted = 2U << 8U;
inline constexpr uintptr_t NoLexer = 4U << 8U;
inline constexpr uintptr_t NoAutoComp = 8U << 8U;
} // namespace SyntaxKeywordAttribute
namespace SyntaxLexerAttribute {
inline constexpr uint32_t TabsAsSpaces = 1U << 0U;
inline constexpr uint32_t NoGlobalTabSettings = 1U << 1U;
inline constexpr uint32_t NoLineComment = 1U << 2U;
inline constexpr uint32_t NoBlockComment = 1U << 3U;
inline constexpr uint32_t IndentBasedFolding = 1U << 4U;
inline constexpr uint32_t IndentLookForward = 1U << 5U;
inline constexpr uint32_t PrintfFormatSpecifier = 1U << 6U;
inline constexpr uint32_t AngleBracketGeneric = 1U << 7U;
inline constexpr uint32_t CppPreprocessor = 1U << 8U;
inline constexpr uint32_t CharacterPrefix = 1U << 9U;
inline constexpr uint32_t EscapePunctuation = 1U << 10U;
inline constexpr uint32_t PlainTextFile = 1U << 11U;
} // namespace SyntaxLexerAttribute

// Pinned EditLexer.h::EDITLEXER generated defaults, shared by editing consumers.
struct SyntaxOriginalMetadata {
    uint32_t lexerAttributes = 0;
    int tabWidth = 4;
    int indentWidth = 4;
    int foldLevelMask = 0;
    int foldIgnoreInner = 0;
    int escapeCharacterStart = 0;
    int escapeCharacterStyle = 0;
    int formatSpecifierStyle = 0;
    int autoIndentWordStyle = 0;
    int characterLiteralStyle = 0;
    int noneSingleQuotedStyle = 0;
    int operatorStyle = 0;
    int operatorStyle2 = 0;
    int commentStyleMarker = 0;
    int stringStyleFirst = 0;
    int stringStyleLast = 0;
};
struct SyntaxProperty {
    const char* name;
    const char* value;
};
struct SyntaxComments {
    const char* line;
    const char* open;
    const char* close;
};
struct SyntaxDefinition {
    const char* name;
    std::span<const char* const> extensions;
    std::span<const SyntaxKeywords> keywords;
    std::span<const SyntaxStyle> styles;
    std::span<const SyntaxProperty> properties;
    int lexer;
    SyntaxLanguage language;
    bool canFold;
    const char* lineComment;
    const char* blockCommentOpen = nullptr;
    const char* blockCommentClose = nullptr;
    int commentStyleLast = 0;
    bool indentLookForward = false;
    bool encodingHeader = false;
    const char* scriptInterpreter = nullptr;
    SyntaxOriginalMetadata original;
};

std::span<const SyntaxDefinition> SyntaxCatalog();
const SyntaxDefinition& SyntaxFor(SyntaxLanguage language);
SyntaxLanguage SyntaxSettingsLanguage(SyntaxLanguage language);
const char* SyntaxSettingsSection(SyntaxLanguage language);
SyntaxLanguage LanguageForPath(const wxString& path);
std::optional<SyntaxLanguage> DetectMarkupDocType(std::string_view prefix);
std::optional<DetectedSyntax> DetectLanguage(const wxString& path, std::string_view prefix,
                                             SyntaxDetectionOptions options = {});
wxString LanguageName(SyntaxLanguage language);
bool LanguageCanFold(SyntaxLanguage language);
std::span<const SyntaxStyle> MarkupEmbeddedStyles();
SyntaxComments SyntaxCommentsFor(SyntaxLanguage language, int style);
