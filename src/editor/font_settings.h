#pragma once

#include "syntax/syntax_catalog.h"
#include <wx/font.h>
#include <wx/colour.h>
#include <array>
#include <utility>

class EditorControl;
struct SchemeFontStyle {
    bool useCode = true;
    wxString defaultStyle;
};
// Original Global Styles and per-lexer Default/UseDefaultCodeStyle records.
// Retain style text so a font-only edit does not discard other style attributes.
struct FontSettings {
    wxString codeStyle = "font:$(Code)";
    wxString textStyle = "font:$(Text)";
    std::array<SchemeFontStyle, static_cast<size_t>(std::to_underlying(SyntaxLanguage::Count))> schemes = [] {
        std::array<SchemeFontStyle, static_cast<size_t>(std::to_underlying(SyntaxLanguage::Count))> values{};
        values[static_cast<size_t>(std::to_underlying(SyntaxLanguage::PlainText))].useCode = false;
        return values;
    }();
    SchemeFontStyle& Scheme(SyntaxLanguage language);
    const SchemeFontStyle& Scheme(SyntaxLanguage language) const;
};
wxFont DefaultStyleFont(const FontSettings& settings, bool code);
wxColour DefaultStyleForeground(const FontSettings& settings, bool code);
wxString MergeChosenFont(const wxString& style, const wxFont& font);
void ApplyFontSettings(EditorControl& editor, SyntaxLanguage language);
