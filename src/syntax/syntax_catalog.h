#pragma once

#include <wx/string.h>
#include <cstdint>
#include <span>

enum class SyntaxLanguage : uint8_t { PlainText, Cpp, Python, Json };
struct SyntaxStyle {
    int id;
    unsigned int foreground;
    unsigned int background;
    bool bold;
};
struct SyntaxKeywords {
    const char* text;
    uintptr_t attributes;
};
struct SyntaxProperty {
    const char* name;
    const char* value;
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
};

std::span<const SyntaxDefinition> SyntaxCatalog();
const SyntaxDefinition& SyntaxFor(SyntaxLanguage language);
SyntaxLanguage LanguageForPath(const wxString& path);
wxString LanguageName(SyntaxLanguage language);
bool LanguageCanFold(SyntaxLanguage language);
