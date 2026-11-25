#pragma once

#include "syntax/syntax_catalog.h"
#include <string_view>

// File associations and original bounded content sniffing share one precedence
// policy. Catalog metadata remains independent of document-detection requests.
namespace SyntaxDetectionLimits {
// Original Styles.cpp lookup lengths, excluding the terminating NUL.
inline constexpr std::size_t PrefixBytes = 255;
inline constexpr std::size_t DocTypeBytes = 3999;
} // namespace SyntaxDetectionLimits

struct SyntaxDetectionOptions {
    bool autoSelect = true;
    bool noCgiGuess = false;
    bool noHtmlGuess = false;
    SyntaxLanguage defaultLanguage = SyntaxLanguage::PlainText;
    SyntaxExtensions extensions;
    wxString favoriteSchemes;
};
struct DetectedSyntax {
    // Original pLex is chosen before the independent np2LexLangIndex sniff.
    SyntaxLanguage schemeLanguage;
    std::optional<SyntaxLanguage> markupDialect = std::nullopt;
};

SyntaxLanguage LanguageForPath(const wxString& path, const SyntaxExtensions& extensions = {},
                               const wxString& favoriteSchemes = {});
std::optional<SyntaxLanguage> DetectMarkupDocType(std::string_view prefix);
std::optional<DetectedSyntax> DetectLanguage(const wxString& path, std::string_view prefix,
                                             SyntaxDetectionOptions options = {});
