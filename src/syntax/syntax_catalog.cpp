#include "syntax/syntax_catalog.h"
#include <SciLexer.h>
#include <wx/filename.h>
#include <wx/translation.h>
#include <array>
#include <cstdlib>
#include <algorithm>
#include <limits>

namespace {
#include "syntax/generated/cpp_scheme_data.h"
#include "syntax/generated/java_scheme_data.h"
#include "syntax/generated/csharp_scheme_data.h"
#include "syntax/generated/kotlin_scheme_data.h"
#include "syntax/generated/python_scheme_data.h"
#include "syntax/generated/json_scheme_data.h"
#include "syntax/generated/bash_scheme_data.h"
#include "syntax/generated/javascript_scheme_data.h"
#include "syntax/generated/typescript_scheme_data.h"
#include "syntax/generated/markup_scheme_data.h"
#include "syntax/generated/html_scheme_data.h"
#include "syntax/generated/xml_scheme_data.h"
#include "syntax/generated/rust_scheme_data.h"
#include "syntax/generated/go_scheme_data.h"
#include "syntax/generated/lua_scheme_data.h"
#include "syntax/generated/markdown_scheme_data.h"
#include "syntax/generated/toml_scheme_data.h"
#include "syntax/generated/yaml_scheme_data.h"
#include "syntax/generated/cmake_scheme_data.h"
#include "syntax/generated/css_scheme_data.h"
#include "syntax/generated/sql_scheme_data.h"
#include "syntax/generated/detection_extensions.h"

constexpr auto textExtensions = std::to_array<const char*>({"txt"});
constexpr auto cppExtensions = std::to_array<const char*>({"c", "cpp", "cxx", "cc", "h", "hpp", "hxx", "hh"});
constexpr auto pythonExtensions = std::to_array<const char*>({"py", "pyw", "pyi"});
constexpr auto jsonExtensions = std::to_array<const char*>({"json"});
constexpr auto bashExtensions = std::to_array<const char*>({"sh", "zsh", "bash", "in"});
constexpr auto cShellExtensions = std::to_array<const char*>({"csh", "tcsh"});
constexpr auto m4Extensions = std::to_array<const char*>({"m4", "ac"});
constexpr auto cppProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "0"}});
constexpr auto bashProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "0"}});
constexpr auto cShellProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "1"}});
constexpr auto m4Properties = std::to_array<SyntaxProperty>({{"lexer.lang", "2"}});
constexpr auto cssProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "0"}});
constexpr auto scssProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "1"}});
constexpr auto lessProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "2"}});
constexpr auto hssProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "3"}});
// LexSQL.cxx::SqlOption_Default enables backtick identifiers, # comments and
// backslash escapes. Install it explicitly after the shared dialect reset.
constexpr auto sqlProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "7"}});
constexpr auto scssExtensions = std::to_array<const char*>({"scss"});
constexpr auto lessExtensions = std::to_array<const char*>({"less"});
constexpr auto hssExtensions = std::to_array<const char*>({"hss"});
constexpr auto markdownGitHubProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "0"}});
constexpr auto markdownGitLabProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "1"}});
constexpr auto markdownPandocProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "2"}});
constexpr auto jsxExtensions = std::to_array<const char*>({"jsx"});
constexpr auto tsxExtensions = std::to_array<const char*>({"tsx"});
constexpr auto javascriptProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "0"}});
constexpr auto jsxProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "1"}});
constexpr auto typescriptProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "2"}});
constexpr auto tsxProperties = std::to_array<SyntaxProperty>({{"lexer.lang", "3"}});
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
constexpr auto htmlProperties = std::to_array<SyntaxProperty>({{"lexer.lang", htmlOptions.data()}});
constexpr auto htmlVbProperties = std::to_array<SyntaxProperty>({{"lexer.lang", htmlVbOptions.data()}});
constexpr auto jspProperties = std::to_array<SyntaxProperty>({{"lexer.lang", jspOptions.data()}});
constexpr auto jspxProperties = std::to_array<SyntaxProperty>({{"lexer.lang", jspxOptions.data()}});
constexpr auto xmlProperties = std::to_array<SyntaxProperty>({{"lexer.lang", xmlOptions.data()}});
constexpr auto svgProperties = std::to_array<SyntaxProperty>({{"lexer.lang", svgOptions.data()}});
constexpr auto aspExtensions = std::to_array<const char*>({"asp"});
constexpr auto aspxExtensions = std::to_array<const char*>({"aspx"});
constexpr auto jspExtensions = std::to_array<const char*>({"jsp"});
constexpr auto jspxExtensions = std::to_array<const char*>({"jspx"});
constexpr auto svgExtensions = std::to_array<const char*>({"svg"});
constexpr auto xsdExtensions = std::to_array<const char*>({"xsd"});
constexpr auto xsltExtensions = std::to_array<const char*>({"xsl", "xslt"});
constexpr auto dtdExtensions = std::to_array<const char*>({"dtd"});
constexpr auto plistExtensions = std::to_array<const char*>({"plist"});
// Pinned stlDefault.cpp::lexTextFile uses the distinct Text Settings table.
constexpr SyntaxOriginalMetadata plainOriginal = [] {
    SyntaxOriginalMetadata original{SyntaxLexerAttribute::NoLineComment | SyntaxLexerAttribute::NoBlockComment |
                                        SyntaxLexerAttribute::IndentBasedFolding |
                                        SyntaxLexerAttribute::IndentLookForward | SyntaxLexerAttribute::PlainTextFile,
                                    4, 4, (1 << 1) | (1 << 2)};
    original.schemeId = 0; // NP2LEX_TEXTFILE - NP2LEX_TEXTFILE.
    return original;
}();
constexpr auto menuFamilies = std::to_array<SyntaxMenuGroup>({
    // TRANSLATORS: Original Scheme/Text File family.
    {SyntaxMenuFamily::TextFile, wxTRANSLATE("&Text File")},
    // TRANSLATORS: Original Scheme/CSS Style Sheet family and dialect order.
    {SyntaxMenuFamily::Css, wxTRANSLATE("&CSS Style Sheet")},
    // TRANSLATORS: Original Scheme/JavaScript family, including TypeScript and JSX/TSX.
    {SyntaxMenuFamily::JavaScript, wxTRANSLATE("&JavaScript")},
    // TRANSLATORS: Original Scheme/Web Source Code family, including HTML, JSP and ASP.
    {SyntaxMenuFamily::Html, wxTRANSLATE("&Web Source Code")},
    // TRANSLATORS: Original Scheme/Markdown family and dialect order.
    {SyntaxMenuFamily::Markdown, wxTRANSLATE("Mark&down")},
    // TRANSLATORS: Original Scheme/Shell Script family, including Bash, C Shell and m4.
    {SyntaxMenuFamily::Shell, wxTRANSLATE("Shell Scri&pt")},
    // TRANSLATORS: Original Scheme/XML Document family and document dialects.
    {SyntaxMenuFamily::Xml, wxTRANSLATE("&XML Document")},
});
constexpr auto definitions = std::to_array<SyntaxDefinition>({
    {wxTRANSLATE("Plain Text"),
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
     true,
     false,
     nullptr,
     plainOriginal,
     {SyntaxMenuFamily::TextFile, 0}},
    {wxTRANSLATE("C/C++"),
     cppExtensions,
     cppLists,
     cppStyles,
     cppProperties,
     SCLEX_CPP,
     SyntaxLanguage::Cpp,
     true,
     "//",
     "/*",
     "*/",
     SCE_C_COMMENTDOC_TAG_XML,
     false,
     false,
     nullptr,
     cppOriginal,
     {SyntaxMenuFamily::Standalone, 0}},
    {wxTRANSLATE("C# Source"),
     csharpExtensions,
     csharpLists,
     csharpStyles,
     {},
     SCLEX_CSHARP,
     SyntaxLanguage::CSharp,
     true,
     "//",
     "/*",
     "*/",
     SCE_CSHARP_TASKMARKER,
     false,
     false,
     nullptr,
     csharpOriginal,
     {SyntaxMenuFamily::Standalone, 10}},
    {wxTRANSLATE("Java Source"),
     javaExtensions,
     javaLists,
     javaStyles,
     {},
     SCLEX_JAVA,
     SyntaxLanguage::Java,
     true,
     "//",
     "/*",
     "*/",
     SCE_JAVA_TASKMARKER,
     false,
     false,
     nullptr,
     javaOriginal,
     {SyntaxMenuFamily::Standalone, 40}},
    {wxTRANSLATE("Kotlin Source"),
     kotlinExtensions,
     kotlinLists,
     kotlinStyles,
     {},
     SCLEX_KOTLIN,
     SyntaxLanguage::Kotlin,
     true,
     "//",
     "/*",
     "*/",
     SCE_KOTLIN_TASKMARKER,
     false,
     false,
     nullptr,
     kotlinOriginal,
     {SyntaxMenuFamily::Standalone, 60}},
    {wxTRANSLATE("Python"),
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
     "python3",
     pythonOriginal,
     {SyntaxMenuFamily::Standalone, 80}},
    {wxTRANSLATE("JSON"),
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
     SCE_JSON_BLOCKCOMMENT,
     false,
     false,
     nullptr,
     jsonOriginal,
     {SyntaxMenuFamily::Standalone, 50}},
    {wxTRANSLATE("JavaScript"),
     javascriptExtensions,
     javascriptLists,
     javascriptStyles,
     javascriptProperties,
     SCLEX_JAVASCRIPT,
     SyntaxLanguage::JavaScript,
     true,
     "//",
     "/*",
     "*/",
     SCE_JS_TASKMARKER,
     false,
     false,
     "node",
     javascriptOriginal,
     {SyntaxMenuFamily::JavaScript, 0}},
    {wxTRANSLATE("JSX"),
     jsxExtensions,
     javascriptLists,
     javascriptStyles,
     jsxProperties,
     SCLEX_JAVASCRIPT,
     SyntaxLanguage::Jsx,
     true,
     "//",
     "/*",
     "*/",
     SCE_JS_TASKMARKER,
     false,
     false,
     "node",
     javascriptOriginal,
     {SyntaxMenuFamily::JavaScript, 1}},
    {wxTRANSLATE("TypeScript"),
     typescriptExtensions,
     typescriptLists,
     typescriptStyles,
     typescriptProperties,
     SCLEX_JAVASCRIPT,
     SyntaxLanguage::TypeScript,
     true,
     "//",
     "/*",
     "*/",
     SCE_JS_TASKMARKER,
     false,
     false,
     nullptr,
     typescriptOriginal,
     {SyntaxMenuFamily::JavaScript, 2}},
    {wxTRANSLATE("TSX"),
     tsxExtensions,
     typescriptLists,
     typescriptStyles,
     tsxProperties,
     SCLEX_JAVASCRIPT,
     SyntaxLanguage::Tsx,
     true,
     "//",
     "/*",
     "*/",
     SCE_JS_TASKMARKER,
     false,
     false,
     nullptr,
     typescriptOriginal,
     {SyntaxMenuFamily::JavaScript, 3}},
    {wxTRANSLATE("Bash"),
     bashExtensions,
     bashLists,
     bashStyles,
     bashProperties,
     SCLEX_BASH,
     SyntaxLanguage::Bash,
     true,
     "#",
     nullptr,
     nullptr,
     SCE_SH_COMMENTLINE,
     false,
     false,
     "/bin/bash",
     bashOriginal,
     {SyntaxMenuFamily::Shell, 0}},
    {wxTRANSLATE("C Shell"),
     cShellExtensions,
     bashLists,
     bashStyles,
     cShellProperties,
     SCLEX_BASH,
     SyntaxLanguage::CShell,
     true,
     "#",
     nullptr,
     nullptr,
     SCE_SH_COMMENTLINE,
     false,
     false,
     "/bin/csh",
     bashOriginal,
     {SyntaxMenuFamily::Shell, 1}},
    {wxTRANSLATE("m4"),
     m4Extensions,
     bashLists,
     bashStyles,
     m4Properties,
     SCLEX_BASH,
     SyntaxLanguage::M4,
     true,
     "dnl ",
     nullptr,
     nullptr,
     SCE_SH_COMMENTLINE,
     false,
     false,
     "m4",
     bashOriginal,
     {SyntaxMenuFamily::Shell, 2}},
    {wxTRANSLATE("Web Source Code"),
     htmlExtensions,
     htmlLists,
     htmlStyles,
     htmlProperties,
     SCLEX_HTML,
     SyntaxLanguage::Html,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     htmlOriginal,
     {SyntaxMenuFamily::Html, 0}},
    {wxTRANSLATE("ASP VBScript"),
     aspExtensions,
     htmlLists,
     htmlStyles,
     htmlVbProperties,
     SCLEX_HTML,
     SyntaxLanguage::AspVb,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     htmlOriginal,
     {SyntaxMenuFamily::Html, 5}},
    {wxTRANSLATE("ASP JavaScript"),
     aspExtensions,
     htmlLists,
     htmlStyles,
     htmlProperties,
     SCLEX_HTML,
     SyntaxLanguage::AspJs,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     htmlOriginal,
     {SyntaxMenuFamily::Html, 6}},
    {wxTRANSLATE("ASP.NET C#"),
     aspxExtensions,
     htmlLists,
     htmlStyles,
     htmlProperties,
     SCLEX_HTML,
     SyntaxLanguage::AspxCs,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     htmlOriginal,
     {SyntaxMenuFamily::Html, 3}},
    {wxTRANSLATE("ASP.NET VB"),
     aspxExtensions,
     htmlLists,
     htmlStyles,
     htmlVbProperties,
     SCLEX_HTML,
     SyntaxLanguage::AspxVb,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     htmlOriginal,
     {SyntaxMenuFamily::Html, 4}},
    {wxTRANSLATE("JSP"),
     jspExtensions,
     htmlLists,
     htmlStyles,
     jspProperties,
     SCLEX_HTML,
     SyntaxLanguage::Jsp,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     htmlOriginal,
     {SyntaxMenuFamily::Html, 1}},
    {wxTRANSLATE("JSPX"),
     jspxExtensions,
     htmlLists,
     htmlStyles,
     jspxProperties,
     SCLEX_HTML,
     SyntaxLanguage::Jspx,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     htmlOriginal,
     {SyntaxMenuFamily::Html, 2}},
    {wxTRANSLATE("XML Document"),
     xmlExtensions,
     xmlLists,
     xmlStyles,
     xmlProperties,
     SCLEX_HTML,
     SyntaxLanguage::Xml,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     xmlOriginal,
     {SyntaxMenuFamily::Xml, 0}},
    {wxTRANSLATE("SVG"),
     svgExtensions,
     xmlLists,
     xmlStyles,
     svgProperties,
     SCLEX_HTML,
     SyntaxLanguage::Svg,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     xmlOriginal,
     {SyntaxMenuFamily::Xml, 5}},
    {wxTRANSLATE("XML Schema"),
     xsdExtensions,
     xmlLists,
     xmlStyles,
     xmlProperties,
     SCLEX_HTML,
     SyntaxLanguage::Xsd,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     xmlOriginal,
     {SyntaxMenuFamily::Xml, 1}},
    {wxTRANSLATE("XSLT"),
     xsltExtensions,
     xmlLists,
     xmlStyles,
     xmlProperties,
     SCLEX_HTML,
     SyntaxLanguage::Xslt,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     xmlOriginal,
     {SyntaxMenuFamily::Xml, 2}},
    {wxTRANSLATE("DTD"),
     dtdExtensions,
     xmlLists,
     xmlStyles,
     xmlProperties,
     SCLEX_HTML,
     SyntaxLanguage::Dtd,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     xmlOriginal,
     {SyntaxMenuFamily::Xml, 3}},
    {wxTRANSLATE("Property List"),
     plistExtensions,
     xmlLists,
     xmlStyles,
     xmlProperties,
     SCLEX_HTML,
     SyntaxLanguage::Plist,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     xmlOriginal,
     {SyntaxMenuFamily::Xml, 4}},
    {wxTRANSLATE("Rust"),
     rustExtensions,
     rustLists,
     rustStyles,
     {},
     SCLEX_RUST,
     SyntaxLanguage::Rust,
     true,
     "//",
     "/*",
     "*/",
     SCE_RUST_TASKMARKER,
     false,
     false,
     nullptr,
     rustOriginal,
     {SyntaxMenuFamily::Standalone, 90}},
    {wxTRANSLATE("Go"),
     goExtensions,
     goLists,
     goStyles,
     {},
     SCLEX_GO,
     SyntaxLanguage::Go,
     true,
     "//",
     "/*",
     "*/",
     SCE_GO_TASKMARKER,
     false,
     false,
     nullptr,
     goOriginal,
     {SyntaxMenuFamily::Standalone, 30}},
    {wxTRANSLATE("Lua"),
     luaExtensions,
     luaLists,
     luaStyles,
     {},
     SCLEX_LUA,
     SyntaxLanguage::Lua,
     true,
     "--",
     "--[[",
     "]]",
     SCE_LUA_COMMENTLINE,
     false,
     false,
     "lua",
     luaOriginal,
     {SyntaxMenuFamily::Standalone, 70}},
    {wxTRANSLATE("GitHub GFM"),
     markdownExtensions,
     markdownLists,
     markdownStyles,
     markdownGitHubProperties,
     SCLEX_MARKDOWN,
     SyntaxLanguage::MarkdownGitHub,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     markdownOriginal,
     {SyntaxMenuFamily::Markdown, 0}},
    {wxTRANSLATE("GitLab GLFM"),
     {},
     markdownLists,
     markdownStyles,
     markdownGitLabProperties,
     SCLEX_MARKDOWN,
     SyntaxLanguage::MarkdownGitLab,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     markdownOriginal,
     {SyntaxMenuFamily::Markdown, 1}},
    {wxTRANSLATE("Pandoc"),
     {},
     markdownLists,
     markdownStyles,
     markdownPandocProperties,
     SCLEX_MARKDOWN,
     SyntaxLanguage::MarkdownPandoc,
     true,
     nullptr,
     "<!--",
     "-->",
     SCE_H_COMMENT,
     false,
     false,
     nullptr,
     markdownOriginal,
     {SyntaxMenuFamily::Markdown, 2}},
    {wxTRANSLATE("TOML"),
     tomlExtensions,
     tomlLists,
     tomlStyles,
     {},
     SCLEX_TOML,
     SyntaxLanguage::Toml,
     true,
     "#",
     nullptr,
     nullptr,
     SCE_TOML_COMMENT,
     false,
     false,
     nullptr,
     tomlOriginal,
     {SyntaxMenuFamily::Standalone, 110}},
    {wxTRANSLATE("YAML"),
     yamlExtensions,
     yamlLists,
     yamlStyles,
     {},
     SCLEX_YAML,
     SyntaxLanguage::Yaml,
     true,
     "#",
     nullptr,
     nullptr,
     SCE_YAML_COMMENT,
     true,
     false,
     nullptr,
     yamlOriginal,
     {SyntaxMenuFamily::Standalone, 120}},
    {wxTRANSLATE("CMake"),
     cmakeExtensions,
     cmakeLists,
     cmakeStyles,
     {},
     SCLEX_CMAKE,
     SyntaxLanguage::CMake,
     true,
     "#",
     "#[[",
     "]]",
     SCE_CMAKE_TASKMARKER,
     false,
     false,
     nullptr,
     cmakeOriginal,
     {SyntaxMenuFamily::Standalone, 20}},
    // TRANSLATORS: Original CSS stylesheet scheme title.
    {wxTRANSLATE("CSS Style Sheet"),
     cssExtensions,
     cssLists,
     cssStyles,
     cssProperties,
     SCLEX_CSS,
     SyntaxLanguage::Css,
     true,
     nullptr,
     "/*",
     "*/",
     SCE_CSS_CDO_CDC,
     false,
     false,
     nullptr,
     cssOriginal,
     {SyntaxMenuFamily::Css, 0}},
    // TRANSLATORS: Original Sass SCSS stylesheet scheme title.
    {wxTRANSLATE("SCSS Style Sheet"),
     scssExtensions,
     cssLists,
     cssStyles,
     scssProperties,
     SCLEX_CSS,
     SyntaxLanguage::Scss,
     true,
     "//",
     "/*",
     "*/",
     SCE_CSS_CDO_CDC,
     false,
     false,
     nullptr,
     cssOriginal,
     {SyntaxMenuFamily::Css, 1}},
    // TRANSLATORS: Original Less stylesheet scheme title.
    {wxTRANSLATE("Less Style Sheet"),
     lessExtensions,
     cssLists,
     cssStyles,
     lessProperties,
     SCLEX_CSS,
     SyntaxLanguage::Less,
     true,
     "//",
     "/*",
     "*/",
     SCE_CSS_CDO_CDC,
     false,
     false,
     nullptr,
     cssOriginal,
     {SyntaxMenuFamily::Css, 2}},
    // TRANSLATORS: Original HSS stylesheet scheme title.
    {wxTRANSLATE("HSS Style Sheet"),
     hssExtensions,
     cssLists,
     cssStyles,
     hssProperties,
     SCLEX_CSS,
     SyntaxLanguage::Hss,
     true,
     "//",
     "/*",
     "*/",
     SCE_CSS_CDO_CDC,
     false,
     false,
     nullptr,
     cssOriginal,
     {SyntaxMenuFamily::Css, 3}},
    // TRANSLATORS: Original SQL Query scheme title.
    {wxTRANSLATE("SQL Query"),
     sqlExtensions,
     sqlLists,
     sqlStyles,
     sqlProperties,
     SCLEX_SQL,
     SyntaxLanguage::Sql,
     true,
     "--",
     "/*",
     "*/",
     SCE_SQL_COMMENTLINEDOC,
     false,
     false,
     nullptr,
     sqlOriginal,
     {SyntaxMenuFamily::Standalone, 100}},
});

bool Space(char ch) {
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

wxString FileExtension(const wxString& path) {
    // PathFindExtension treats the leading dot in a dotfile as its extension.
    const auto name = wxFileName(path).GetFullName();
    return name.Contains('.') ? name.AfterLast('.') : wxString{};
}

std::string_view InterpreterToken(std::string_view text, size_t& position) {
    while (position < text.size() && (text[position] == ' ' || text[position] == '\t'))
        ++position;
    size_t name = position;
    while (position < text.size() && text[position] && !Space(text[position])) {
        if (text[position] == '/' || text[position] == '\\') name = position + 1;
        ++position;
    }
    return text.substr(name, position - name);
}

// Adapted from Notepad4 89b8b36 Styles.cpp::Style_SniffShebang.
// Only delivered interpreter results are retained. The original prefix and token
// rules intentionally accept py*/ipy* and do not parse env options or quotes.
std::optional<SyntaxLanguage> SniffInterpreter(std::string_view text) {
    using enum SyntaxLanguage;
    text = text.substr(0, SyntaxDetectionLimits::PrefixBytes);
    if (!text.starts_with("#!")) return std::nullopt;
    size_t position = 2;
    auto name = InterpreterToken(text, position);
    if (name.starts_with("env") || name.starts_with("winpty")) name = InterpreterToken(text, position);
    std::string letters;
    for (const auto ch : name) {
        const char lower = static_cast<char>(ch | 0x20);
        if (lower < 'a' || lower > 'z') break;
        letters += lower;
    }
    if (letters.starts_with("py") || letters.starts_with("ipy")) return Python;
    if (letters.starts_with("tcsh") || letters.starts_with("csh")) return CShell;
    for (const auto* shell : {"bash", "dash", "ash", "zsh", "ksh", "sh"}) {
        if (letters.starts_with(shell)) return Bash;
    }
    if (letters.starts_with("node")) return JavaScript;
    if (letters.starts_with("lua") || letters.starts_with("wlua")) return Lua;
    return std::nullopt;
}

} // namespace

// Adapted from pinned Styles.cpp::Style_GetDocTypeLanguage. Its 4000-byte
// header lookup precedes the ASP language directive and does not parse XML.
std::optional<SyntaxLanguage> DetectMarkupDocType(std::string_view text) {
    using enum SyntaxLanguage;
    std::string lower(text.substr(0, SyntaxDetectionLimits::DocTypeBytes));
    for (auto& ch : lower) {
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    }
    auto position = lower.find("<!doctype");
    if (position != std::string::npos) {
        auto type = std::string_view(lower).substr(position + 9);
        while (!type.empty() && Space(type.front()))
            type.remove_prefix(1);
        if (type.starts_with("html")) return Html;
        // The original schema check is case-sensitive.
        if (text.substr(lower.size() - type.size()).starts_with("schema")) return Xsd;
    }
    position = text.find("<%@");
    if (position == std::string_view::npos) return std::nullopt;
    position = lower.find("language", position + 3);
    if (position == std::string::npos) return std::nullopt;
    auto language = std::string_view(lower).substr(std::min(position + 9, lower.size()));
    while (!language.empty() && (language.front() == ' ' || language.front() == '=' || language.front() == '"'))
        language.remove_prefix(1);
    if (language.starts_with("c#")) return AspxCs;
    if (language.starts_with("vbscript")) return AspVb;
    if (language.starts_with("vb")) return AspxVb;
    if (language.starts_with("jscript") || language.starts_with("javascript")) return AspJs;
    if (language.starts_with("java")) return Jsp;
    return std::nullopt;
}

namespace {
enum class FilenameMatch : uint8_t { Unknown, Associated, Config };
FilenameMatch OriginalFilenameMatch(const wxString& path) {
    using enum FilenameMatch;
    const wxFileName file(path);
    const auto name = file.GetFullName().Lower();
    const auto extension = FileExtension(path).Lower().ToStdString();
    // The original treats MySQL ini/cnf files as Config, permitting sniffing.
    if (name.StartsWith("my") && (extension == "ini" || extension == "cnf")) return Config;
    size_t start = 0;
    while (start < originalAssociatedExtensions.size()) {
        const auto end = originalAssociatedExtensions.find(';', start);
        if (originalAssociatedExtensions.substr(start, end - start) == extension) return Associated;
        start = end + 1;
    }
    start = 0;
    while (start < originalConfigExtensions.size()) {
        const auto end = originalConfigExtensions.find(';', start);
        if (originalConfigExtensions.substr(start, end - start) == extension) return Config;
        start = end + 1;
    }
    for (const auto* prefix : {"readme", "makefile", "kbuild", "jamfile"}) {
        if (name.StartsWith(prefix)) return Associated;
    }
    for (const auto* exact : {"cakefile", "rakefile", "podfile", "steepfile", "mozconfig", "apkbuild", "pkgbuild",
                              "jamroot", "emakefile"}) {
        if (name.CmpNoCase(exact) == 0) return Associated;
    }
    return Unknown;
}

bool MarkupNameStart(unsigned char ch) {
    return ch >= 128 || ch == '!' || ch == '?' || ch == '/' || ch == ':' || ch == '_' || ch == '-' || ch == '.' ||
           (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

std::optional<SyntaxLanguage> SniffMarkupFamily(std::string_view prefix, FilenameMatch match, bool noHtmlGuess) {
    using enum SyntaxLanguage;
    // Styles.cpp::Style_SetLexerFromFile: only unknown names and Config may
    // inspect markup, and Config only accepts the exact XML declaration.
    auto head = prefix.substr(0, SyntaxDetectionLimits::PrefixBytes);
    while (!head.empty() && Space(head.front()))
        head.remove_prefix(1);
    if (!head.starts_with("<")) return std::nullopt;
    if (head.starts_with("<?xml")) return Xml;
    if (match != FilenameMatch::Unknown || head.starts_with("<?php")) return std::nullopt;

    std::string lower(head);
    for (auto& ch : lower) {
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    }
    if (lower.starts_with("<!doctype")) return Xml;
    if (lower.starts_with("<html")) return Html;
    if (noHtmlGuess || head.size() <= 1) return std::nullopt;
    if (lower.contains("<html")) return Html;
    if (MarkupNameStart(static_cast<unsigned char>(head[1]))) return Xml;
    return std::nullopt;
}
} // namespace

std::span<const SyntaxDefinition> SyntaxCatalog() {
    return definitions;
}
std::span<const SyntaxMenuGroup> SyntaxMenuFamilies() {
    return menuFamilies;
}
std::vector<const SyntaxDefinition*> SyntaxMenuMembers(SyntaxMenuFamily family) {
    std::vector<const SyntaxDefinition*> members;
    members.reserve(SyntaxCatalog().size());
    for (const auto& definition : SyntaxCatalog())
        if (definition.menu.family == family) members.push_back(&definition);
    std::stable_sort(members.begin(), members.end(),
                     [](const auto* left, const auto* right) { return left->menu.order < right->menu.order; });
    return members;
}

const SyntaxDefinition& SyntaxFor(SyntaxLanguage language) {
    for (const auto& definition : definitions) {
        if (definition.language == language) return definition;
    }
    return definitions[0];
}

std::optional<SyntaxLanguage> SyntaxFromOriginalSchemeId(int identity) {
    if (identity < 0) return std::nullopt;
    for (const auto& definition : SyntaxCatalog()) {
        if (definition.original.schemeId == identity &&
            SyntaxSettingsLanguage(definition.language) == definition.language)
            return definition.language;
    }
    return std::nullopt;
}

std::vector<int> OriginalFavoriteSchemeIds(const wxString& configuration) {
    // Pinned Helpers.cpp::ParseCommaList: decimal values, optional commas and whitespace.
    const auto bytes = configuration.Left(SyntaxSchemeLimits::FavoriteConfigCharacters).utf8_string();
    const char* cursor = bytes.c_str();
    std::vector<int> identities;
    for (size_t count = 0; count < SyntaxSchemeLimits::FavoriteCount; ++count) {
        char* end = nullptr;
        const auto identity = std::strtol(cursor, &end, 10);
        if (end == cursor) break;
        if (identity >= std::numeric_limits<int>::min() && identity <= std::numeric_limits<int>::max())
            identities.push_back(static_cast<int>(identity));
        cursor = *end == ',' ? end + 1 : end;
    }
    return identities;
}

std::vector<SyntaxLanguage> FavoriteSyntaxSchemes(const wxString& configuration) {
    std::vector<SyntaxLanguage> languages;
    for (const int identity : OriginalFavoriteSchemeIds(configuration)) {
        if (identity <= 0) continue;
        if (const auto language = SyntaxFromOriginalSchemeId(identity);
            language && std::ranges::find(languages, *language) == languages.end())
            languages.push_back(*language);
    }
    return languages;
}

SyntaxLanguage SyntaxSettingsLanguage(SyntaxLanguage language) {
    using enum SyntaxLanguage;
    if (language >= Html && language <= Jspx) return Html;
    if (language >= Xml && language <= Plist) return Xml;
    switch (language) {
    case Scss:
    case Less:
    case Hss:
        return Css;
    case CShell:
    case M4:
        return Bash;
    case Jsx:
        return JavaScript;
    case Tsx:
        return TypeScript;
    case MarkdownGitLab:
    case MarkdownPandoc:
        return MarkdownGitHub;
    default:
        return language;
    }
}

const char* SyntaxSettingsSection(SyntaxLanguage language) {
    switch (SyntaxSettingsLanguage(language)) {
        using enum SyntaxLanguage;
    case Cpp:
        return "C/C++ Source";
    case Java:
        return javaSettingsSection;
    case CSharp:
        return csharpSettingsSection;
    case Kotlin:
        return kotlinSettingsSection;
    case Python:
        return "Python Script";
    case Json:
        return "JSON Document";
    case Html:
        return "Web Source Code";
    case Xml:
        return "XML Document";
    case Bash:
        return "Shell Script";
    case JavaScript:
        return "JavaScript";
    case TypeScript:
        return "TypeScript";
    case Rust:
        return rustSettingsSection;
    case Go:
        return goSettingsSection;
    case Lua:
        return luaSettingsSection;
    case MarkdownGitHub:
        return markdownSettingsSection;
    case Toml:
        return tomlSettingsSection;
    case Yaml:
        return yamlSettingsSection;
    case CMake:
        return cmakeSettingsSection;
    case Css:
        return cssSettingsSection;
    case Sql:
        return sqlSettingsSection;
    default:
        return "Text File";
    }
}

SyntaxLanguage LanguageForPath(const wxString& path) {
    using enum SyntaxLanguage;
    const auto name = wxFileName(path).GetFullName();
    // Pinned Styles.cpp::Style_GetLexerFromFile checks these .txt names first.
    if (name.CmpNoCase("CMakeLists.txt") == 0 || name.CmpNoCase("CMakeCache.txt") == 0) return CMake;
    if (const auto originalExtension = FileExtension(path);
        name.StartsWith(".") && (originalExtension.StartsWith("bash") || originalExtension == "profile"))
        return Bash;
    wxString extension = FileExtension(path);
    for (size_t index = 0; index < extension.length(); ++index) {
        const auto ch = extension[index].GetValue();
        if (ch >= 'A' && ch <= 'Z') extension[index] = ch + ('a' - 'A');
    }
    // Dialect-specific associations precede their canonical scheme's full list.
    if (extension == "asp") return AspVb;
    if (extension == "aspx") return AspxCs;
    for (size_t index = std::size(definitions); index-- > 0;) {
        const auto& definition = definitions[index];
        for (const auto* candidate : definition.extensions) {
            if (extension == candidate) return definition.language;
        }
    }
    for (const auto* build : {"mozconfig", "apkbuild", "pkgbuild"}) {
        if (name.CmpNoCase(build) == 0) return Bash;
    }
    return PlainText;
}

std::optional<DetectedSyntax> DetectLanguage(const wxString& path, std::string_view prefix,
                                             SyntaxDetectionOptions options) {
    using enum SyntaxLanguage;
    if (!options.autoSelect) return std::nullopt;
    const auto extension = FileExtension(path).Lower();
    const auto interpreter = options.noCgiGuess ? std::nullopt : SniffInterpreter(prefix);
    bool templateConfig = false;
    // Original .in handling strips one suffix before matching the underlying
    // filename. An unmatched template keeps the Shell Script association.
    if (extension == "in") {
        const auto underlying = path.BeforeLast('.');
        if (const auto language = LanguageForPath(underlying); language != PlainText) return DetectedSyntax{language};
        if (OriginalFilenameMatch(underlying) == FilenameMatch::Associated) return DetectedSyntax{PlainText};
        templateConfig = OriginalFilenameMatch(underlying) == FilenameMatch::Config;
    }
    const auto language = templateConfig ? PlainText : LanguageForPath(path);
    if (language != PlainText) {
        if (extension == "asp" || extension == "aspx" || extension == "xml")
            return DetectedSyntax{SyntaxSettingsLanguage(language), DetectMarkupDocType(prefix).value_or(language)};
        return DetectedSyntax{language};
    }
    if (extension == "txt") return DetectedSyntax{language};
    // CGI sniffing precedes associated extensions; unmatched names are sniffed
    // only after original filename matches. Config files are sniffed too.
    if ((extension == "cgi" || extension == "fcgi") && interpreter) return DetectedSyntax{*interpreter};
    const auto match = templateConfig ? FilenameMatch::Config : OriginalFilenameMatch(path);
    if (match == FilenameMatch::Associated) return DetectedSyntax{PlainText};
    if (auto markup = SniffMarkupFamily(prefix, match, options.noHtmlGuess)) {
        const auto dialect = DetectMarkupDocType(prefix);
        // Pinned Style_SetLexerFromFile promotes a prefix-selected XML
        // family only for WEB; other sniffed dialects retain that family.
        if (*markup == Xml && dialect == Html) markup = Html;
        return DetectedSyntax{*markup, dialect};
    }
    if (interpreter) return DetectedSyntax{*interpreter};
    return match == FilenameMatch::Config ? std::optional{DetectedSyntax{PlainText}} : std::nullopt;
}

wxString LanguageName(SyntaxLanguage language) {
    return wxGetTranslation(SyntaxFor(language).name);
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
