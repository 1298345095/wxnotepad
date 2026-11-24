#include "editor/font_settings.h"
#include "editor/editor_control.h"
#include <wx/display.h>
#include <wx/fontenum.h>
#include <wx/settings.h>
#include <wx/tokenzr.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <charconv>
#include <cstdlib>
#include <optional>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
using Scintilla::Message;
constexpr auto DefaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
constexpr int NormalWeight = 400;
constexpr int BoldWeight = 700;
constexpr int MinimumFontSize = 2 * Scintilla::FontSizeMultiplier;
wxString Value(const wxString& style, const wxString& key) {
    const auto position = style.find(key + ':');
    return position == wxString::npos ? wxString{}
                                      : style.Mid(position + key.length() + 1).BeforeFirst(';').Strip(wxString::both);
}
std::optional<int> Integer(const wxString& text) {
    const auto bytes = text.utf8_string();
    char* end = nullptr;
    const long value = std::strtol(bytes.c_str(), &end, 10);
    if (end == bytes.c_str()) return std::nullopt;
    return static_cast<int>(std::clamp(value, static_cast<long>(std::numeric_limits<int>::min()),
                                       static_cast<long>(std::numeric_limits<int>::max())));
}
std::optional<unsigned> Colour(const wxString& text) {
    if (!text.StartsWith('#')) return std::nullopt;
    const auto bytes = text.Mid(1).utf8_string();
    char* end = nullptr;
    const auto rgb = std::strtoul(bytes.c_str(), &end, 16);
    if (end == bytes.c_str()) return std::nullopt;
    return static_cast<unsigned>(((rgb & 0xffU) << 16U) | (rgb & 0xff00U) | ((rgb >> 16U) & 0xffU));
}
bool Attribute(const wxString& style, const wxString& name) {
    wxStringTokenizer tokens(style, ";");
    while (tokens.HasMoreTokens())
        if (tokens.GetNextToken().Trim().Trim(false) == name) return true;
    return false;
}
wxString CodeFamily() {
    // Notepad4 Styles.cpp::FindSystemDefaultCodeFont, in the original order.
    for (const auto* name : {"DejaVu Sans Mono", "Cascadia Mono", "Consolas"})
        if (wxFontEnumerator::IsValidFacename(name)) return name;
#ifdef __WXMSW__
    return "Courier New";
#else
    return wxFont(wxFontInfo(11).Family(wxFONTFAMILY_TELETYPE)).GetFaceName();
#endif
}
wxString TextFamily() {
    return wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).GetFaceName();
}
int BaseSize() {
    // Styles.cpp::Style_DetectBaseFontSize uses physical monitor dimensions.
    const auto display = wxDisplay().GetGeometry().GetSize();
    const double points = display.x < 1920 && display.y < 1080 ? 10.5 : 11;
    return static_cast<int>(points * Scintilla::FontSizeMultiplier);
}
std::optional<int> Size(const wxString& style, int base) {
    const auto text = Value(style, "size");
    auto bytes = text.utf8_string();
    std::string_view number(bytes);
    if (number.starts_with('+')) number.remove_prefix(1);
    float points = 0;
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), points);
    if (parsed.ptr == number.data() || parsed.ec != std::errc{} || !std::isfinite(points)) return std::nullopt;
    const bool relative = text.StartsWith('+') || text.StartsWith('-');
    // Keep the original 2pt floor and the core's integer size representation.
    const double scaled = points * Scintilla::FontSizeMultiplier + (relative ? base : 0);
    return static_cast<int>(std::clamp(std::round(scaled), static_cast<double>(MinimumFontSize),
                                       static_cast<double>(std::numeric_limits<int>::max())));
}
int Weight(const wxString& style, int fallback) {
    if (Attribute(style, "bold")) return BoldWeight;
    const auto weight = Integer(Value(style, "weight"));
    return weight && *weight > 0 && *weight < 1000 ? *weight : fallback;
}
wxString Family(const wxString& style, const FontSettings& settings, bool global) {
    const auto face = Value(style, "font");
    if (face.empty()) return {};
    if (face == "$(Text)") return global ? TextFamily() : DefaultStyleFont(settings, false).GetFaceName();
    if (face == "$(Code)" || !wxFontEnumerator::IsValidFacename(face))
        return global ? CodeFamily() : DefaultStyleFont(settings, true).GetFaceName();
    return face;
}
void ApplyStyle(EditorControl& editor, const FontSettings& settings, const wxString& style, int base,
                uintptr_t target = DefaultStyle) {
    if (!Value(style, "font").empty()) {
        const auto family = Family(style, settings, false);
        const auto face = family.ToUTF8();
        editor.Send(Message::StyleSetFont, target, reinterpret_cast<intptr_t>(face.data()));
    }
    if (const auto size = Size(style, base)) editor.Send(Message::StyleSetSizeFractional, target, *size);
    editor.Send(Message::StyleSetWeight, target,
                Weight(style, static_cast<int>(editor.Send(Message::StyleGetWeight, target))));
    for (const auto& [attribute, message] : {std::pair{"italic", Message::StyleSetItalic},
                                             {"underline", Message::StyleSetUnderline},
                                             {"strike", Message::StyleSetStrike},
                                             {"overline", Message::StyleSetOverline},
                                             {"eolfilled", Message::StyleSetEOLFilled}})
        if (Attribute(style, attribute)) editor.Send(message, target, true);
    for (const auto& [attribute, message] :
         {std::pair{"fore", Message::StyleSetFore}, {"back", Message::StyleSetBack}}) {
        if (const auto colour = Colour(Value(style, attribute))) editor.Send(message, target, *colour);
    }
    if (const auto charset = Integer(Value(style, "charset")))
        editor.Send(Message::StyleSetCharacterSet, target, *charset);
}
} // namespace

wxString StyleProperty(const wxString& style, const wxString& key) {
    return Value(style, key);
}
bool StyleAttribute(const wxString& style, const wxString& attribute) {
    return Attribute(style, attribute);
}
int StyleInteger(const wxString& style, const wxString& key, int fallback) {
    return Integer(Value(style, key)).value_or(fallback);
}
unsigned StyleColour(const wxString& style, const wxString& key, unsigned fallback) {
    return Colour(Value(style, key)).value_or(fallback);
}
void ApplyStyleText(EditorControl& editor, int id, const wxString& style, int baseSize) {
    ApplyStyle(editor, editor.Fonts(), style, baseSize, static_cast<uintptr_t>(id));
}

SchemeFontStyle& FontSettings::Scheme(SyntaxLanguage language) {
    return schemes[static_cast<size_t>(std::to_underlying(SyntaxSettingsLanguage(language)))];
}
const SchemeFontStyle& FontSettings::Scheme(SyntaxLanguage language) const {
    return schemes[static_cast<size_t>(std::to_underlying(SyntaxSettingsLanguage(language)))];
}
void EditorControl::SetFontSettings(const FontSettings& settings) {
    fontSettings_ = std::make_shared<FontSettings>(settings);
}
const FontSettings& EditorControl::Fonts() const {
    static const FontSettings defaults;
    return fontSettings_ ? *fontSettings_ : defaults;
}
wxFont DefaultStyleFont(const FontSettings& settings, bool code) {
    const auto& style = code ? settings.codeStyle : settings.textStyle;
    auto face = Family(style, settings, true);
    if (face.empty()) face = CodeFamily();
    wxFont font(wxFontInfo(11).FaceName(face));
    font.SetFractionalPointSize(static_cast<double>(Size(style, BaseSize()).value_or(BaseSize())) /
                                Scintilla::FontSizeMultiplier);
    font.SetNumericWeight(Weight(style, NormalWeight));
    font.SetStyle(Attribute(style, "italic") ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL);
    font.SetUnderlined(Attribute(style, "underline"));
    font.SetStrikethrough(Attribute(style, "strike"));
    return font;
}
wxColour DefaultStyleForeground(const FontSettings& settings, bool code) {
    auto colour = StyleSystemColour(wxSYS_COLOUR_WINDOWTEXT);
    colour.SetRGB(StyleColour(code ? settings.codeStyle : settings.textStyle, "fore", colour.GetRGB()));
    return colour;
}
wxColour StyleSystemColour(wxSystemColour colour) {
#ifdef __WXMSW__
    // Original Styles.cpp uses GetSysColor, including high contrast. wxMSW's
    // dark-mode GetColour override belongs to the application chrome palette.
    const auto rgb = ::GetSysColor(colour);
    return wxColour(GetRValue(rgb), GetGValue(rgb), GetBValue(rgb));
#else
    return wxSystemSettings::GetColour(colour);
#endif
}
wxFont StyleFont(const FontSettings& settings, const wxString& style, bool code, bool globalBase) {
    auto font = DefaultStyleFont(settings, code);
    const auto base = static_cast<int>(std::lround(font.GetFractionalPointSize() * Scintilla::FontSizeMultiplier));
    if (const auto face = Family(style, settings, globalBase); !face.empty()) font.SetFaceName(face);
    font.SetFractionalPointSize(static_cast<double>(Size(style, base).value_or(base)) / Scintilla::FontSizeMultiplier);
    font.SetNumericWeight(Weight(style, NormalWeight));
    font.SetStyle(Attribute(style, "italic") ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL);
    font.SetUnderlined(Attribute(style, "underline"));
    font.SetStrikethrough(Attribute(style, "strike"));
    return font;
}
wxString MergeChosenFont(const wxString& style, const wxFont& font) {
    // Adapted from Notepad4 89b8b36 Styles.cpp::Style_SelectFont (BSD-3-Clause).
    wxString result = "font:" + font.GetFaceName() + "; size:";
    auto size = wxString::Format("%.1f", font.GetFractionalPointSize());
    size.Replace(",", ".");
    if (size.EndsWith(".0")) size.RemoveLast(2);
    result += size;
    if (font.GetNumericWeight() == BoldWeight)
        result += "; bold";
    else if (font.GetNumericWeight() != NormalWeight)
        result += wxString::Format("; weight:%d", font.GetNumericWeight());
    if (font.GetStyle() != wxFONTSTYLE_NORMAL) result += "; italic";
    if (font.GetUnderlined()) result += "; underline";
    if (font.GetStrikethrough()) result += "; strike";
    wxStringTokenizer tokens(style, ";");
    while (tokens.HasMoreTokens()) {
        const auto token = tokens.GetNextToken().Trim().Trim(false);
        const auto key = token.BeforeFirst(':');
        if (!token.empty() && key != "font" && key != "size" && key != "weight" && token != "bold" &&
            token != "italic" && token != "underline" && token != "strike")
            result += "; " + token;
    }
    return result;
}
void ApplyFontSettings(EditorControl& editor, SyntaxLanguage language) {
    const auto& settings = editor.Fonts();
    const auto& scheme = settings.Scheme(language);
    const auto& style = scheme.useCode ? settings.codeStyle : settings.textStyle;
    const auto font = DefaultStyleFont(settings, scheme.useCode);
    editor.Send(Message::StyleResetDefault);
    const auto family = font.GetFaceName();
    const auto face = family.ToUTF8();
    editor.Send(Message::StyleSetFont, DefaultStyle, reinterpret_cast<intptr_t>(face.data()));
    const auto size = static_cast<int>(std::lround(font.GetFractionalPointSize() * Scintilla::FontSizeMultiplier));
    editor.Send(Message::StyleSetSizeFractional, DefaultStyle, size);
    editor.Send(Message::StyleSetWeight, DefaultStyle, font.GetNumericWeight());
    editor.Send(Message::StyleSetItalic, DefaultStyle, font.GetStyle() != wxFONTSTYLE_NORMAL);
    editor.Send(Message::StyleSetUnderline, DefaultStyle, font.GetUnderlined());
    editor.Send(Message::StyleSetStrike, DefaultStyle, font.GetStrikethrough());
    const auto foreground = StyleSystemColour(wxSYS_COLOUR_WINDOWTEXT);
    const auto background = StyleSystemColour(wxSYS_COLOUR_WINDOW);
    editor.Send(Message::StyleSetFore, DefaultStyle,
                foreground.Red() | (foreground.Green() << 8U) | (foreground.Blue() << 16U));
    editor.Send(Message::StyleSetBack, DefaultStyle,
                background.Red() | (background.Green() << 8U) | (background.Blue() << 16U));
    ApplyStyle(editor, settings, style, BaseSize());
    ApplyStyle(editor, settings, scheme.defaultStyle, size);
}
