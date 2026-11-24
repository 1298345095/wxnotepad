#pragma once

#include "syntax/syntax_catalog.h"
#include "syntax/style_profile.h"
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <array>
#include <map>
#include <utility>

class EditorControl;
inline constexpr std::array<unsigned, 16> DefaultCustomColours = {
    0xB000B0, 0xFF8000, 0x008000, 0x608060, 0xA46000, 0x0080FF, 0xFFF1AB, 0xFFC040,
    0x1E90FF, 0x648000, 0x006633, 0x408080, 0x008080, 0xFF0080, 0x408040, 0x0080C0};
struct SchemeFontStyle {
    bool useCode = true;
    wxString defaultStyle;
    std::map<wxString, wxString> tokenStyles;
    bool operator==(const SchemeFontStyle&) const = default;
};
// Original Global Styles and per-lexer Default/UseDefaultCodeStyle records.
// Retain style text so a font-only edit does not discard other style attributes.
struct FontSettings {
    StyleTheme theme = StyleTheme::Default;
    std::array<unsigned, 16> customColours = DefaultCustomColours;
    std::map<wxString, wxString> globalStyles;
    wxString codeStyle = "font:$(Code)";
    wxString textStyle = "font:$(Text)";
    std::array<SchemeFontStyle, static_cast<size_t>(std::to_underlying(SyntaxLanguage::Count))> schemes = [] {
        std::array<SchemeFontStyle, static_cast<size_t>(std::to_underlying(SyntaxLanguage::Count))> values{};
        values[static_cast<size_t>(std::to_underlying(SyntaxLanguage::PlainText))].useCode = false;
        return values;
    }();
    SchemeFontStyle& Scheme(SyntaxLanguage language);
    const SchemeFontStyle& Scheme(SyntaxLanguage language) const;
    wxString GlobalStyle(const wxString& key) const;
    wxString TokenStyle(SyntaxLanguage language, const StyleDefinition& definition) const;
    bool operator==(const FontSettings&) const = default;
};
wxFont DefaultStyleFont(const FontSettings& settings, bool code);
wxFont StyleFont(const FontSettings& settings, const wxString& style, bool code, bool globalBase = false);
wxColour DefaultStyleForeground(const FontSettings& settings, bool code);
wxColour StyleSystemColour(wxSystemColour colour);
wxString MergeChosenFont(const wxString& style, const wxFont& font);
void ApplyFontSettings(EditorControl& editor, SyntaxLanguage language);
wxString StyleProperty(const wxString& style, const wxString& key);
bool StyleAttribute(const wxString& style, const wxString& attribute);
int StyleInteger(const wxString& style, const wxString& key, int fallback);
unsigned StyleColour(const wxString& style, const wxString& key, unsigned fallback);
void ApplyStyleText(EditorControl& editor, int id, const wxString& style, int baseSize);
void ApplyStyleProfile(EditorControl& editor, SyntaxLanguage language);
