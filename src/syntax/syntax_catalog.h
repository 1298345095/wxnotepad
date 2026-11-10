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
    Count
};
struct SyntaxDetectionOptions {
    bool autoSelect = true;
    bool noCgiGuess = false;
    bool noHtmlGuess = false;
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
};

std::span<const SyntaxDefinition> SyntaxCatalog();
const SyntaxDefinition& SyntaxFor(SyntaxLanguage language);
SyntaxLanguage LanguageForPath(const wxString& path);
std::optional<SyntaxLanguage> DetectLanguage(const wxString& path, std::string_view prefix,
                                             SyntaxDetectionOptions options = {});
wxString LanguageName(SyntaxLanguage language);
bool LanguageCanFold(SyntaxLanguage language);
SyntaxLanguage SyntaxSettingsLanguage(SyntaxLanguage language);
const char* SyntaxSettingsSection(SyntaxLanguage language);
std::span<const SyntaxStyle> MarkupEmbeddedStyles();
SyntaxComments SyntaxCommentsFor(SyntaxLanguage language, int style);
