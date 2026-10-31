#include "syntax/syntax_catalog.h"
#include <SciLexer.h>
#include <wx/filename.h>
#include <array>

namespace {
#include "syntax/generated/cpp_scheme_data.h"
#include "syntax/generated/python_scheme_data.h"
#include "syntax/generated/json_scheme_data.h"

template <size_t N> constexpr auto Keywords(const char* const (&text)[N], uintptr_t attributes) {
    std::array<SyntaxKeywords, N> result{};
    for (size_t index = 0; index < N; ++index)
        result[index] = {text[index], attributes};
    return result;
}
constexpr auto cppLists = Keywords(cppKeywords, 0);
constexpr auto pythonLists = Keywords(pythonKeywords, 2U << 8U);
constexpr auto jsonLists = Keywords(jsonKeywords, jsonKeywordAttributes);
constexpr const char* textExtensions[] = {"txt"};
constexpr const char* cppExtensions[] = {"c", "cpp", "cxx", "cc", "h", "hpp", "hxx", "hh"};
constexpr const char* pythonExtensions[] = {"py", "pyw", "pyi"};
constexpr const char* jsonExtensions[] = {"json"};
constexpr SyntaxProperty cppProperties[] = {{"lexer.lang", "0"}};
constexpr SyntaxDefinition definitions[] = {
    {"Plain Text", textExtensions, {}, {}, {}, SCLEX_NULL, SyntaxLanguage::PlainText, false},
    {"C/C++", cppExtensions, cppLists, cppStyles, cppProperties, SCLEX_CPP, SyntaxLanguage::Cpp, true},
    {"Python", pythonExtensions, pythonLists, pythonStyles, {}, SCLEX_PYTHON, SyntaxLanguage::Python, true},
    {"JSON", jsonExtensions, jsonLists, jsonStyles, {}, SCLEX_JSON, SyntaxLanguage::Json, true},
};
} // namespace

std::span<const SyntaxDefinition> SyntaxCatalog() {
    return definitions;
}

const SyntaxDefinition& SyntaxFor(SyntaxLanguage language) {
    for (const auto& definition : definitions) {
        if (definition.language == language) return definition;
    }
    return definitions[0];
}

SyntaxLanguage LanguageForPath(const wxString& path) {
    wxString extension = wxFileName(path).GetExt();
    for (size_t index = 0; index < extension.length(); ++index) {
        const auto ch = extension[index].GetValue();
        if (ch >= 'A' && ch <= 'Z') extension[index] = ch + ('a' - 'A');
    }
    for (const auto& definition : definitions) {
        for (const auto* candidate : definition.extensions) {
            if (extension == candidate) return definition.language;
        }
    }
    return SyntaxLanguage::PlainText;
}

wxString LanguageName(SyntaxLanguage language) {
    return SyntaxFor(language).name;
}
bool LanguageCanFold(SyntaxLanguage language) {
    return SyntaxFor(language).canFold;
}
