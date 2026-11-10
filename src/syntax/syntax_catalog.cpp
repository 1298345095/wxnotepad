#include "syntax/syntax_catalog.h"
#include <SciLexer.h>
#include <wx/filename.h>
#include <array>

namespace {
#include "syntax/generated/cpp_scheme_data.h"
#include "syntax/generated/python_scheme_data.h"
#include "syntax/generated/json_scheme_data.h"
#include "syntax/generated/javascript_scheme_data.h"
#include "syntax/generated/typescript_scheme_data.h"
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
constexpr const char* jsxExtensions[] = {"jsx"};
constexpr const char* tsxExtensions[] = {"tsx"};
constexpr SyntaxProperty javascriptProperties[] = {{"lexer.lang", "0"}};
constexpr SyntaxProperty jsxProperties[] = {{"lexer.lang", "1"}};
constexpr SyntaxProperty typescriptProperties[] = {{"lexer.lang", "2"}};
constexpr SyntaxProperty tsxProperties[] = {{"lexer.lang", "3"}};
constexpr SyntaxDefinition definitions[] = {
    {"Plain Text",
     textExtensions,
     {},
     {},
     {},
     SCLEX_NULL,
     SyntaxLanguage::PlainText,
     true,
     nullptr,
     nullptr,
     nullptr,
     0,
     true},
    {"C/C++", cppExtensions, cppLists, cppStyles, cppProperties, SCLEX_CPP, SyntaxLanguage::Cpp, true, "//", "/*", "*/",
     SCE_C_COMMENTDOC_TAG_XML},
    {"Python",
     pythonExtensions,
     pythonLists,
     pythonStyles,
     {},
     SCLEX_PYTHON,
     SyntaxLanguage::Python,
     true,
     "#",
     nullptr,
     nullptr,
     SCE_PY_TASKMARKER,
     true,
     true,
     "python3"},
    {"JSON",
     jsonExtensions,
     jsonLists,
     jsonStyles,
     {},
     SCLEX_JSON,
     SyntaxLanguage::Json,
     true,
     "//",
     "/*",
     "*/",
     SCE_JSON_BLOCKCOMMENT},
    {"JavaScript", javascriptExtensions, javascriptLists, javascriptStyles, javascriptProperties, SCLEX_JAVASCRIPT,
     SyntaxLanguage::JavaScript, true, "//", "/*", "*/", SCE_JS_TASKMARKER, false, false, "node"},
    {"JSX", jsxExtensions, javascriptLists, javascriptStyles, jsxProperties, SCLEX_JAVASCRIPT, SyntaxLanguage::Jsx,
     true, "//", "/*", "*/", SCE_JS_TASKMARKER, false, false, "node"},
    {"TypeScript", typescriptExtensions, typescriptLists, typescriptStyles, typescriptProperties, SCLEX_JAVASCRIPT,
     SyntaxLanguage::TypeScript, true, "//", "/*", "*/", SCE_JS_TASKMARKER},
    {"TSX", tsxExtensions, typescriptLists, typescriptStyles, tsxProperties, SCLEX_JAVASCRIPT, SyntaxLanguage::Tsx,
     true, "//", "/*", "*/", SCE_JS_TASKMARKER},
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
// Only delivered interpreter results are retained. The original prefix and token
// rules intentionally accept py*/ipy* and do not parse env options or quotes.
std::optional<SyntaxLanguage> SniffInterpreter(std::string_view text) {
    if (!text.starts_with("#!")) return std::nullopt;
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
    if (letters.starts_with("py") || letters.starts_with("ipy")) return SyntaxLanguage::Python;
    if (letters.starts_with("node")) return SyntaxLanguage::JavaScript;
    return std::nullopt;
}

enum class FilenameMatch : uint8_t { Unknown, Associated, Config };
FilenameMatch OriginalFilenameMatch(const wxString& path) {
    const wxFileName file(path);
    const auto name = file.GetFullName().Lower();
    const auto extension = FileExtension(path).Lower().ToStdString();
    if (name.StartsWith(".") && (extension.starts_with("bash") || extension == "profile"))
        return FilenameMatch::Associated;
    // The original treats MySQL ini/cnf files as Config, permitting sniffing.
    if (name.StartsWith("my") && (extension == "ini" || extension == "cnf")) return FilenameMatch::Config;
    size_t start = 0;
    while (start < originalAssociatedExtensions.size()) {
        const auto end = originalAssociatedExtensions.find(';', start);
        if (originalAssociatedExtensions.substr(start, end - start) == extension) return FilenameMatch::Associated;
        start = end + 1;
    }
    start = 0;
    while (start < originalConfigExtensions.size()) {
        const auto end = originalConfigExtensions.find(';', start);
        if (originalConfigExtensions.substr(start, end - start) == extension) return FilenameMatch::Config;
        start = end + 1;
    }
    for (const auto* prefix : {"readme", "makefile", "kbuild", "jamfile"}) {
        if (name.StartsWith(prefix)) return FilenameMatch::Associated;
    }
    for (const auto* exact : {"cakefile", "rakefile", "podfile", "steepfile", "mozconfig", "apkbuild", "pkgbuild",
                              "jamroot", "emakefile"}) {
        if (name.CmpNoCase(exact) == 0) return FilenameMatch::Associated;
    }
    return FilenameMatch::Unknown;
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

SyntaxLanguage SyntaxSettingsLanguage(SyntaxLanguage language) {
    switch (language) {
    case SyntaxLanguage::Jsx:
        return SyntaxLanguage::JavaScript;
    case SyntaxLanguage::Tsx:
        return SyntaxLanguage::TypeScript;
    default:
        return language;
    }
}

const char* SyntaxSettingsSection(SyntaxLanguage language) {
    switch (SyntaxSettingsLanguage(language)) {
    case SyntaxLanguage::Cpp:
        return "C/C++ Source";
    case SyntaxLanguage::Python:
        return "Python Script";
    case SyntaxLanguage::Json:
        return "JSON Document";
    case SyntaxLanguage::JavaScript:
        return "JavaScript";
    case SyntaxLanguage::TypeScript:
        return "TypeScript";
    default:
        return "Text File";
    }
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

std::optional<SyntaxLanguage> DetectLanguage(const wxString& path, std::string_view prefix,
                                             SyntaxDetectionOptions options) {
    if (!options.autoSelect) return std::nullopt;
    const auto language = LanguageForPath(path);
    if (language != SyntaxLanguage::PlainText || FileExtension(path).CmpNoCase("txt") == 0) return language;
    const auto extension = FileExtension(path).Lower();
    // CGI sniffing precedes associated extensions; unmatched names are sniffed
    // only after original filename matches. Config files are sniffed too.
    const auto interpreter = options.noCgiGuess ? std::nullopt : SniffInterpreter(prefix);
    if ((extension == "cgi" || extension == "fcgi") && interpreter) return interpreter;
    const auto match = OriginalFilenameMatch(path);
    if (match == FilenameMatch::Associated) return SyntaxLanguage::PlainText;
    if (interpreter) return interpreter;
    return match == FilenameMatch::Config ? std::optional{SyntaxLanguage::PlainText} : std::nullopt;
}

wxString LanguageName(SyntaxLanguage language) {
    return SyntaxFor(language).name;
}
bool LanguageCanFold(SyntaxLanguage language) {
    return SyntaxFor(language).canFold;
}
