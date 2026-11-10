#include "syntax/syntax_catalog.h"
#include <SciLexer.h>
#include <wx/filename.h>
#include <array>

namespace {
#include "syntax/generated/cpp_scheme_data.h"
#include "syntax/generated/python_scheme_data.h"
#include "syntax/generated/json_scheme_data.h"
#include "syntax/generated/bash_scheme_data.h"
#include "syntax/generated/javascript_scheme_data.h"
#include "syntax/generated/typescript_scheme_data.h"
#include "syntax/generated/markup_scheme_data.h"
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
constexpr auto bashLists = Keywords(bashKeywords, 2U << 8U);
constexpr const char* textExtensions[] = {"txt"};
constexpr const char* cppExtensions[] = {"c", "cpp", "cxx", "cc", "h", "hpp", "hxx", "hh"};
constexpr const char* pythonExtensions[] = {"py", "pyw", "pyi"};
constexpr const char* jsonExtensions[] = {"json"};
constexpr const char* bashExtensions[] = {"sh", "zsh", "bash", "in"};
constexpr const char* cShellExtensions[] = {"csh", "tcsh"};
constexpr const char* m4Extensions[] = {"m4", "ac"};
constexpr SyntaxProperty cppProperties[] = {{"lexer.lang", "0"}};
constexpr SyntaxProperty bashProperties[] = {{"lexer.lang", "0"}};
constexpr SyntaxProperty cShellProperties[] = {{"lexer.lang", "1"}};
constexpr SyntaxProperty m4Properties[] = {{"lexer.lang", "2"}};
constexpr const char* jsxExtensions[] = {"jsx"};
constexpr const char* tsxExtensions[] = {"tsx"};
constexpr SyntaxProperty javascriptProperties[] = {{"lexer.lang", "0"}};
constexpr SyntaxProperty jsxProperties[] = {{"lexer.lang", "1"}};
constexpr SyntaxProperty typescriptProperties[] = {{"lexer.lang", "2"}};
constexpr SyntaxProperty tsxProperties[] = {{"lexer.lang", "3"}};
// Pinned Styles.cpp / LexHTML.cxx HtmlOption contract. lexer.lang stores
// these flags as little endian bytes, rather than a decimal property string.
enum HtmlOption : unsigned {
    HtmlIsXml = 1,
    HtmlAllowScript = 2,
    HtmlAllowStyle = 4,
    HtmlAllowAsp = 8,
    HtmlAllowJspEl = 16,
    HtmlAllowJspx = 32,
    HtmlJavaScript = 1 << 12,
    HtmlVbScript = 2 << 12,
    HtmlBase = HtmlAllowScript | HtmlAllowStyle | HtmlAllowAsp,
    JspBase = HtmlAllowScript | HtmlAllowStyle | HtmlAllowJspEl | HtmlJavaScript,
    XmlDefault = HtmlIsXml | HtmlJavaScript,
};
constexpr auto HtmlOptionBytes(unsigned options) {
    return std::array{static_cast<char>(options), static_cast<char>(options >> 8), '\0'};
}
constexpr auto htmlOptions = HtmlOptionBytes(HtmlBase | HtmlJavaScript);
constexpr auto htmlVbOptions = HtmlOptionBytes(HtmlBase | HtmlVbScript);
constexpr auto jspOptions = HtmlOptionBytes(JspBase | HtmlAllowAsp);
constexpr auto jspxOptions = HtmlOptionBytes(JspBase | HtmlAllowJspx | HtmlIsXml);
constexpr auto xmlOptions = HtmlOptionBytes(XmlDefault);
constexpr auto svgOptions = HtmlOptionBytes(XmlDefault | HtmlAllowStyle);
constexpr SyntaxProperty htmlProperties[] = {{"lexer.lang", htmlOptions.data()}};
constexpr SyntaxProperty htmlVbProperties[] = {{"lexer.lang", htmlVbOptions.data()}};
constexpr SyntaxProperty jspProperties[] = {{"lexer.lang", jspOptions.data()}};
constexpr SyntaxProperty jspxProperties[] = {{"lexer.lang", jspxOptions.data()}};
constexpr SyntaxProperty xmlProperties[] = {{"lexer.lang", xmlOptions.data()}};
constexpr SyntaxProperty svgProperties[] = {{"lexer.lang", svgOptions.data()}};
constexpr const char* aspExtensions[] = {"asp"};
constexpr const char* aspxExtensions[] = {"aspx"};
constexpr const char* jspExtensions[] = {"jsp"};
constexpr const char* jspxExtensions[] = {"jspx"};
constexpr const char* svgExtensions[] = {"svg"};
constexpr const char* xsdExtensions[] = {"xsd"};
constexpr const char* xsltExtensions[] = {"xsl", "xslt"};
constexpr const char* dtdExtensions[] = {"dtd"};
constexpr const char* plistExtensions[] = {"plist"};
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
    {"Bash", bashExtensions, bashLists, bashStyles, bashProperties, SCLEX_BASH, SyntaxLanguage::Bash, true, "#",
     nullptr, nullptr, SCE_SH_COMMENTLINE, false, false, "/bin/bash"},
    {"C Shell", cShellExtensions, bashLists, bashStyles, cShellProperties, SCLEX_BASH, SyntaxLanguage::CShell, true,
     "#", nullptr, nullptr, SCE_SH_COMMENTLINE, false, false, "/bin/csh"},
    {"m4", m4Extensions, bashLists, bashStyles, m4Properties, SCLEX_BASH, SyntaxLanguage::M4, true, "dnl ", nullptr,
     nullptr, SCE_SH_COMMENTLINE, false, false, "m4"},
    {"Web Source Code", htmlExtensions, htmlLists, htmlStyles, htmlProperties, SCLEX_HTML, SyntaxLanguage::Html, true,
     nullptr, "<!--", "-->", SCE_H_COMMENT},
    {"ASP VBScript", aspExtensions, htmlLists, htmlStyles, htmlVbProperties, SCLEX_HTML, SyntaxLanguage::AspVb, true,
     nullptr, "<!--", "-->", SCE_H_COMMENT},
    {"ASP JavaScript", aspExtensions, htmlLists, htmlStyles, htmlProperties, SCLEX_HTML, SyntaxLanguage::AspJs, true,
     nullptr, "<!--", "-->", SCE_H_COMMENT},
    {"ASP.NET C#", aspxExtensions, htmlLists, htmlStyles, htmlProperties, SCLEX_HTML, SyntaxLanguage::AspxCs, true,
     nullptr, "<!--", "-->", SCE_H_COMMENT},
    {"ASP.NET VB", aspxExtensions, htmlLists, htmlStyles, htmlVbProperties, SCLEX_HTML, SyntaxLanguage::AspxVb, true,
     nullptr, "<!--", "-->", SCE_H_COMMENT},
    {"JSP", jspExtensions, htmlLists, htmlStyles, jspProperties, SCLEX_HTML, SyntaxLanguage::Jsp, true, nullptr, "<!--",
     "-->", SCE_H_COMMENT},
    {"JSPX", jspxExtensions, htmlLists, htmlStyles, jspxProperties, SCLEX_HTML, SyntaxLanguage::Jspx, true, nullptr,
     "<!--", "-->", SCE_H_COMMENT},
    {"XML Document", xmlExtensions, xmlLists, xmlStyles, xmlProperties, SCLEX_HTML, SyntaxLanguage::Xml, true, nullptr,
     "<!--", "-->", SCE_H_COMMENT},
    {"SVG", svgExtensions, xmlLists, xmlStyles, svgProperties, SCLEX_HTML, SyntaxLanguage::Svg, true, nullptr, "<!--",
     "-->", SCE_H_COMMENT},
    {"XML Schema", xsdExtensions, xmlLists, xmlStyles, xmlProperties, SCLEX_HTML, SyntaxLanguage::Xsd, true, nullptr,
     "<!--", "-->", SCE_H_COMMENT},
    {"XSLT", xsltExtensions, xmlLists, xmlStyles, xmlProperties, SCLEX_HTML, SyntaxLanguage::Xslt, true, nullptr,
     "<!--", "-->", SCE_H_COMMENT},
    {"DTD", dtdExtensions, xmlLists, xmlStyles, xmlProperties, SCLEX_HTML, SyntaxLanguage::Dtd, true, nullptr, "<!--",
     "-->", SCE_H_COMMENT},
    {"Property List", plistExtensions, xmlLists, xmlStyles, xmlProperties, SCLEX_HTML, SyntaxLanguage::Plist, true,
     nullptr, "<!--", "-->", SCE_H_COMMENT},
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
    text = text.substr(0, 255);
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
    if (letters.starts_with("tcsh") || letters.starts_with("csh")) return SyntaxLanguage::CShell;
    for (const auto* shell : {"bash", "dash", "ash", "zsh", "ksh", "sh"}) {
        if (letters.starts_with(shell)) return SyntaxLanguage::Bash;
    }
    if (letters.starts_with("node")) return SyntaxLanguage::JavaScript;
    return std::nullopt;
}

} // namespace

// Adapted from pinned Styles.cpp::Style_GetDocTypeLanguage. Its 4000-byte
// header lookup precedes the ASP language directive and does not parse XML.
std::optional<SyntaxLanguage> DetectMarkupDocType(std::string_view text) {
    std::string lower(text.substr(0, 3999));
    for (auto& ch : lower) {
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    }
    auto position = lower.find("<!doctype");
    if (position != std::string::npos) {
        auto type = std::string_view(lower).substr(position + 9);
        while (!type.empty() && Space(type.front()))
            type.remove_prefix(1);
        if (type.starts_with("html")) return SyntaxLanguage::Html;
        // The original schema check is case-sensitive.
        if (text.substr(lower.size() - type.size()).starts_with("schema")) return SyntaxLanguage::Xsd;
    }
    position = text.find("<%@");
    if (position == std::string_view::npos) return std::nullopt;
    position = lower.find("language", position + 3);
    if (position == std::string::npos) return std::nullopt;
    auto language = std::string_view(lower).substr(std::min(position + 9, lower.size()));
    while (!language.empty() && (language.front() == ' ' || language.front() == '=' || language.front() == '"'))
        language.remove_prefix(1);
    if (language.starts_with("c#")) return SyntaxLanguage::AspxCs;
    if (language.starts_with("vbscript")) return SyntaxLanguage::AspVb;
    if (language.starts_with("vb")) return SyntaxLanguage::AspxVb;
    if (language.starts_with("jscript") || language.starts_with("javascript")) return SyntaxLanguage::AspJs;
    if (language.starts_with("java")) return SyntaxLanguage::Jsp;
    return std::nullopt;
}

namespace {
enum class FilenameMatch : uint8_t { Unknown, Associated, Config };
FilenameMatch OriginalFilenameMatch(const wxString& path) {
    const wxFileName file(path);
    const auto name = file.GetFullName().Lower();
    const auto extension = FileExtension(path).Lower().ToStdString();
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
    if (language >= SyntaxLanguage::Html && language <= SyntaxLanguage::Jspx) return SyntaxLanguage::Html;
    if (language >= SyntaxLanguage::Xml && language <= SyntaxLanguage::Plist) return SyntaxLanguage::Xml;
    switch (language) {
    case SyntaxLanguage::CShell:
    case SyntaxLanguage::M4:
        return SyntaxLanguage::Bash;
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
    case SyntaxLanguage::Html:
        return "Web Source Code";
    case SyntaxLanguage::Xml:
        return "XML Document";
    case SyntaxLanguage::Bash:
        return "Shell Script";
    case SyntaxLanguage::JavaScript:
        return "JavaScript";
    case SyntaxLanguage::TypeScript:
        return "TypeScript";
    default:
        return "Text File";
    }
}

SyntaxLanguage LanguageForPath(const wxString& path) {
    const auto name = wxFileName(path).GetFullName();
    const auto originalExtension = FileExtension(path);
    if (name.StartsWith(".") && (originalExtension.StartsWith("bash") || originalExtension == "profile"))
        return SyntaxLanguage::Bash;
    wxString extension = FileExtension(path);
    for (size_t index = 0; index < extension.length(); ++index) {
        const auto ch = extension[index].GetValue();
        if (ch >= 'A' && ch <= 'Z') extension[index] = ch + ('a' - 'A');
    }
    // Dialect-specific associations precede their canonical scheme's full list.
    if (extension == "asp") return SyntaxLanguage::AspVb;
    if (extension == "aspx") return SyntaxLanguage::AspxCs;
    for (size_t index = std::size(definitions); index-- > 0;) {
        const auto& definition = definitions[index];
        for (const auto* candidate : definition.extensions) {
            if (extension == candidate) return definition.language;
        }
    }
    for (const auto* build : {"mozconfig", "apkbuild", "pkgbuild"}) {
        if (name.CmpNoCase(build) == 0) return SyntaxLanguage::Bash;
    }
    return SyntaxLanguage::PlainText;
}

std::optional<SyntaxLanguage> DetectLanguage(const wxString& path, std::string_view prefix,
                                             SyntaxDetectionOptions options) {
    if (!options.autoSelect) return std::nullopt;
    const auto extension = FileExtension(path).Lower();
    const auto interpreter = options.noCgiGuess ? std::nullopt : SniffInterpreter(prefix);
    bool templateConfig = false;
    // Original .in handling strips one suffix before matching the underlying
    // filename. An unmatched template keeps the Shell Script association.
    if (extension == "in") {
        const auto underlying = path.BeforeLast('.');
        const auto language = LanguageForPath(underlying);
        if (language != SyntaxLanguage::PlainText) return language;
        if (OriginalFilenameMatch(underlying) == FilenameMatch::Associated) return SyntaxLanguage::PlainText;
        templateConfig = OriginalFilenameMatch(underlying) == FilenameMatch::Config;
    }
    const auto language = templateConfig ? SyntaxLanguage::PlainText : LanguageForPath(path);
    if (language != SyntaxLanguage::PlainText) {
        if (extension == "asp" || extension == "aspx" || extension == "xml")
            return DetectMarkupDocType(prefix).value_or(language);
        return language;
    }
    if (extension == "txt") return language;
    // CGI sniffing precedes associated extensions; unmatched names are sniffed
    // only after original filename matches. Config files are sniffed too.
    if ((extension == "cgi" || extension == "fcgi") && interpreter) return interpreter;
    const auto match = templateConfig ? FilenameMatch::Config : OriginalFilenameMatch(path);
    if (match == FilenameMatch::Associated) return SyntaxLanguage::PlainText;
    // Styles.cpp::Style_SetLexerFromFile: only unknown names and Config may
    // inspect markup, and Config only accepts the exact XML declaration.
    auto head = prefix.substr(0, 255);
    while (!head.empty() && Space(head.front()))
        head.remove_prefix(1);
    if (head.starts_with("<")) {
        std::string lower(head);
        for (auto& ch : lower) {
            if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        }
        std::optional<SyntaxLanguage> markup;
        if (head.starts_with("<?xml"))
            markup = SyntaxLanguage::Xml;
        else if (match == FilenameMatch::Unknown && !head.starts_with("<?php")) {
            if (lower.starts_with("<!doctype"))
                markup = SyntaxLanguage::Xml;
            else if (lower.starts_with("<html"))
                markup = SyntaxLanguage::Html;
            else if (!options.noHtmlGuess && head.size() > 1) {
                const auto start = static_cast<unsigned char>(head[1]);
                if (lower.find("<html") != std::string::npos)
                    markup = SyntaxLanguage::Html;
                else if (start >= 128 || start == '!' || start == '?' || start == '/' || start == ':' || start == '_' ||
                         start == '-' || start == '.' || (start >= '0' && start <= '9') ||
                         (start >= 'a' && start <= 'z') || (start >= 'A' && start <= 'Z'))
                    markup = SyntaxLanguage::Xml;
            }
        }
        if (markup) return DetectMarkupDocType(prefix).value_or(*markup);
    }
    if (interpreter) return interpreter;
    return match == FilenameMatch::Config ? std::optional{SyntaxLanguage::PlainText} : std::nullopt;
}

wxString LanguageName(SyntaxLanguage language) {
    return SyntaxFor(language).name;
}
bool LanguageCanFold(SyntaxLanguage language) {
    return SyntaxFor(language).canFold;
}

std::span<const SyntaxStyle> MarkupEmbeddedStyles() {
    return markupEmbeddedStyles;
}

SyntaxComments SyntaxCommentsFor(SyntaxLanguage language, int style) {
    const auto& syntax = SyntaxFor(language);
    if (syntax.lexer != SCLEX_HTML) return {syntax.lineComment, syntax.blockCommentOpen, syntax.blockCommentClose};
    // EditAutoC.cpp::GetCurrentHtmlTextBlockEx and EditToggleComment{Line,Block}.
    if (style >= css_style(SCE_CSS_DEFAULT)) return {nullptr, "/*", "*/"};
    if (style >= js_style(SCE_JS_DEFAULT)) return {"//", "/*", "*/"};
    if (style >= SCE_HB_DEFAULT) return {"'", nullptr, nullptr};
    if (style >= SCE_H_SGML_DEFAULT) return {nullptr, "--", "--"};
    if (style == SCE_H_CDATA) return {"//", "/*", "*/"};
    return {nullptr, "<!--", "-->"};
}
