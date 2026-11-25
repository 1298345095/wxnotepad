#include "syntax/syntax_detection.h"
#include <wx/filename.h>
#include <wx/tokenzr.h>
#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace {
#include "syntax/generated/detection_extensions.h"

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

bool OriginalCppPreprocessor(std::string_view text) {
    if (text.empty() || text.front() < 'a' || text.front() > 'z') return false;
    const auto length = text.find_first_not_of("abcdefghijklmnopqrstuvwxyz_");
    if (length < text.size()) {
        const auto next = text[length];
        if ((next >= 'A' && next <= 'Z') || (next >= '0' && next <= '9')) return false;
    }
    std::string keyword = " ";
    keyword.append(text.substr(0, length));
    keyword += ' ';
    constexpr size_t CppPreprocessorKeywordGroup = 2;
    const auto keywords = SyntaxFor(SyntaxLanguage::Cpp).keywords[CppPreprocessorKeywordGroup].text;
    return std::string_view(keywords).find(keyword) != std::string_view::npos;
}

// The INI outcome of pinned Styles.cpp::Style_AutoDetect. Preserve its column-0
// brackets and C++/sharp-comment/JSON/YAML priorities without adding guesses for
// those other schemes to the existing detector.
bool SniffIniContent(std::string_view text, bool dotFile) {
    text = text.substr(0, SyntaxDetectionLimits::DocTypeBytes);
    const bool shebang = text.starts_with("#!");
    bool maybeIni = false;
    bool maybeJson = false;
    bool notJson = false;
    const auto character = [text](size_t position) { return position < text.size() ? text[position] : '\0'; };
    size_t position = 0;
    while (position < text.size()) {
        if (text[position] == '[') {
            maybeIni = true;
        } else if (text.substr(position).starts_with("---") && Space(character(position + 3))) {
            position += 3;
            while (character(position) == ' ' || character(position) == '\t')
                ++position;
            const auto next = character(position);
            if (Space(next) || next == '#' || next == '%') return false;
        } else {
            while (character(position) == ' ' || character(position) == '\t')
                ++position;
            const auto next = character(position);
            if (next == '#') {
                if (!(position == 0 && shebang)) {
                    ++position;
                    while (character(position) == ' ' || character(position) == '\t')
                        ++position;
                    if (!(shebang || dotFile) || !OriginalCppPreprocessor(text.substr(position))) return false;
                }
            } else if (next == '/') {
                if (character(position + 1) == '/' || character(position + 1) == '*') return false;
            } else if (next == '{' || next == '}' || next == '[') {
                maybeJson = true;
            } else if (next == '"') {
                maybeJson |= maybeIni;
            } else {
                notJson |= next != '\0';
            }
        }
        position = text.find_first_of("\r\n", position);
        if (position == std::string_view::npos) break;
        while (character(position) == '\r' || character(position) == '\n')
            ++position;
    }
    return maybeIni && !(maybeJson && !notJson);
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

SyntaxLanguage LanguageForPath(const wxString& path, const SyntaxExtensions& extensions,
                               const wxString& favoriteSchemes) {
    using enum SyntaxLanguage;
    const auto name = wxFileName(path).GetFullName();
    const auto catalog = SyntaxCatalog();
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
    // Original MySQL settings use the generic Config scheme and remain eligible
    // for shebang/XML sniffing, rather than the Ini Config File association.
    const auto lowerName = name.Lower();
    if (lowerName.StartsWith("my") && (extension == "ini" || extension == "cnf")) return PlainText;
    // Dialect-specific associations precede their canonical scheme's full list.
    const auto dialect = [&](SyntaxLanguage language) {
        if (language == Html && extension == "asp") return AspVb;
        if (language == Html && extension == "aspx") return AspxCs;
        // Keep delivered dialect identities when their canonical association
        // retains the suffix; custom suffixes use the canonical scheme.
        for (size_t index = catalog.size(); index-- > 0;) {
            const auto& definition = catalog[index];
            if (SyntaxSettingsLanguage(definition.language) != language) continue;
            for (const auto* candidate : definition.extensions)
                if (extension == candidate) return definition.language;
        }
        return language;
    };
    auto associations = originalSyntaxAssociations;
    // Styles.cpp::Style_SetFavoriteSchemes keeps both text schemes fixed, then
    // moves each configured favorite ahead of the remaining original schemes.
    size_t favoriteIndex = 2;
    for (const int identity : OriginalFavoriteSchemeIds(favoriteSchemes)) {
        const auto first = associations.begin() + favoriteIndex;
        const auto found =
            std::find_if(first, associations.end(), [identity](const auto& item) { return item.first == identity; });
        if (found != associations.end()) {
            std::rotate(first, found, found + 1);
            ++favoriteIndex;
        }
    }
    for (const auto& [identity, stockExtensions] : associations) {
        const auto language = SyntaxFromOriginalSchemeId(identity);
        auto effective = language ? SyntaxExtensionsFor(*language, extensions)
                                  : wxString::FromUTF8(stockExtensions.data(), stockExtensions.size());
        if (language && !extensions.contains(*language)) {
            // Delivered dialects share the original canonical association slot.
            // An explicit replacement still replaces that whole scheme's list.
            for (const auto& definition : catalog) {
                if (definition.language == *language || SyntaxSettingsLanguage(definition.language) != *language)
                    continue;
                for (const auto* candidate : definition.extensions)
                    effective += ";" + wxString::FromUTF8(candidate);
            }
        }
        wxStringTokenizer candidates(effective, "; \t\r\n", wxTOKEN_STRTOK);
        while (candidates.HasMoreTokens())
            if (extension.CmpNoCase(candidates.GetNextToken()) == 0) return language ? dialect(*language) : PlainText;
    }
    // Even an original association whose lexer is not delivered precedes the
    // Makefile/Kbuild filename fallback (for example Makefile.rb or Kbuild.cfg).
    if (!extension.empty() && OriginalFilenameMatch("file." + extension) != FilenameMatch::Unknown) return PlainText;
    if (lowerName.StartsWith("makefile") || lowerName.StartsWith("kbuild")) return Makefile;
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
        if (const auto language = LanguageForPath(underlying, options.extensions, options.favoriteSchemes);
            language != PlainText)
            return DetectedSyntax{language};
        if (OriginalFilenameMatch(underlying) == FilenameMatch::Associated) return DetectedSyntax{PlainText};
        templateConfig = OriginalFilenameMatch(underlying) == FilenameMatch::Config;
    }
    const auto language =
        templateConfig ? PlainText : LanguageForPath(path, options.extensions, options.favoriteSchemes);
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
    if (match == FilenameMatch::Unknown && SniffIniContent(prefix, wxFileName(path).GetFullName().StartsWith(".")))
        return DetectedSyntax{Ini};
    return match == FilenameMatch::Config ? std::optional{DetectedSyntax{PlainText}} : std::nullopt;
}
