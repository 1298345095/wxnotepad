#include "syntax/syntax_catalog.h"
#include <SciLexer.h>
#include <wx/filename.h>
#include <array>

namespace {
#include "syntax/generated/cpp_scheme_data.h"
#include "syntax/generated/python_scheme_data.h"
#include "syntax/generated/json_scheme_data.h"
#include "syntax/generated/detection_extensions.h"

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

bool Space(char ch) {
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

wxString FileExtension(const wxString& path) {
    // PathFindExtension treats the leading dot in a dotfile as its extension.
    const auto name = wxFileName(path).GetFullName();
    return name.Contains('.') ? name.AfterLast('.') : wxString{};
}

// Adapted from Notepad4 89b8b36 Styles.cpp::Style_SniffShebang.
// Only the delivered Python result is retained. The original prefix and token
// rules intentionally accept py*/ipy* and do not parse env options or quotes.
bool PythonInterpreter(std::string_view text) {
    if (!text.starts_with("#!")) return false;
    size_t position = 2;
    const auto token = [&]() {
        while (position < text.size() && (text[position] == ' ' || text[position] == '\t'))
            ++position;
        size_t name = position;
        while (position < text.size() && text[position] && !Space(text[position])) {
            if (text[position] == '/' || text[position] == '\\') name = position + 1;
            ++position;
        }
        return text.substr(name, position - name);
    };
    auto name = token();
    if (name.starts_with("env") || name.starts_with("winpty")) name = token();
    std::string letters;
    for (const auto ch : name) {
        const char lower = static_cast<char>(ch | 0x20);
        if (lower < 'a' || lower > 'z') break;
        letters += lower;
    }
    return letters.starts_with("py") || letters.starts_with("ipy");
}

bool OriginalFilenameMatch(const wxString& path) {
    const wxFileName file(path);
    const auto name = file.GetFullName().Lower();
    const auto extension = FileExtension(path).Lower().ToStdString();
    if (name.StartsWith(".") && (extension.starts_with("bash") || extension == "profile")) return true;
    // The original treats MySQL ini/cnf files as Config, permitting sniffing.
    if (name.StartsWith("my") && (extension == "ini" || extension == "cnf")) return false;
    size_t start = 0;
    while (start < originalAssociatedExtensions.size()) {
        const auto end = originalAssociatedExtensions.find(';', start);
        if (originalAssociatedExtensions.substr(start, end - start) == extension) return true;
        start = end + 1;
    }
    start = 0;
    while (start < originalConfigExtensions.size()) {
        const auto end = originalConfigExtensions.find(';', start);
        if (originalConfigExtensions.substr(start, end - start) == extension) return false;
        start = end + 1;
    }
    for (const auto* prefix : {"readme", "makefile", "kbuild", "jamfile"}) {
        if (name.StartsWith(prefix)) return true;
    }
    for (const auto* exact : {"cakefile", "rakefile", "podfile", "steepfile", "mozconfig", "apkbuild", "pkgbuild",
                              "jamroot", "emakefile"}) {
        if (name.CmpNoCase(exact) == 0) return true;
    }
    return false;
}
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
    wxString extension = FileExtension(path);
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

SyntaxLanguage DetectLanguage(const wxString& path, std::string_view prefix, SyntaxDetectionOptions options) {
    if (!options.autoSelect) return SyntaxLanguage::PlainText;
    const auto language = LanguageForPath(path);
    if (language != SyntaxLanguage::PlainText || FileExtension(path).CmpNoCase("txt") == 0) return language;
    const auto extension = FileExtension(path).Lower();
    // CGI sniffing precedes associated extensions; unmatched names are sniffed
    // only after original filename matches. Config files are sniffed too.
    if (!options.noCgiGuess && (extension == "cgi" || extension == "fcgi") && PythonInterpreter(prefix))
        return SyntaxLanguage::Python;
    if (OriginalFilenameMatch(path)) return SyntaxLanguage::PlainText;
    return !options.noCgiGuess && PythonInterpreter(prefix) ? SyntaxLanguage::Python : SyntaxLanguage::PlainText;
}

wxString LanguageName(SyntaxLanguage language) {
    return SyntaxFor(language).name;
}
bool LanguageCanFold(SyntaxLanguage language) {
    return SyntaxFor(language).canFold;
}
