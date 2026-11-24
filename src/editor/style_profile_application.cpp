#include "editor/font_settings.h"
#include "editor/editor_control.h"
#include "editor/editor_ids.h"
#include "syntax/syntax_scheme.h"
#include <wx/settings.h>
#include <algorithm>
#include <cmath>
#include <SciLexer.h>

namespace {
using enum Scintilla::Message;
constexpr auto DefaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
unsigned SystemColour(wxSystemColour colour) {
    return StyleSystemColour(colour).GetRGB();
}
void Indicator(EditorControl& editor, uintptr_t id, const wxString& text, unsigned fallback, int alpha, int outline) {
    editor.Send(IndicSetStyle, id, std::to_underlying(Scintilla::IndicatorStyle::RoundBox));
    editor.Send(IndicSetFore, id, StyleColour(text, "fore", fallback));
    editor.Send(IndicSetAlpha, id, std::clamp(StyleInteger(text, "alpha", alpha), 0, 255));
    editor.Send(IndicSetOutlineAlpha, id, std::clamp(StyleInteger(text, "outline", outline), 0, 255));
}
void InstallTokens(EditorControl& editor, SyntaxLanguage language, int baseSize, int offset = 0, int lastStyle = -1) {
    for (const auto& definition : SyntaxStyleDefinitions(language)) {
        if (wxString::FromUTF8(definition.key) == "Default") continue;
        const auto value = editor.Fonts().TokenStyle(language, definition);
        for (size_t index = 0; index < definition.count; ++index)
            ApplyStyleText(editor, definition.ids[index] + offset, value, baseSize);
        if (lastStyle >= 0 && definition.count && definition.ids[0] == lastStyle) break;
    }
}
} // namespace

wxString FontSettings::GlobalStyle(const wxString& key) const {
    if (key == "Default Code Style") return codeStyle;
    if (key == "Default Text Style") return textStyle;
    if (auto found = globalStyles.find(key); found != globalStyles.end()) return found->second;
    for (const auto& definition : GlobalStyleDefinitions())
        if (key == definition.key) return wxString::FromUTF8(definition.defaultValue);
    return {};
}
wxString FontSettings::TokenStyle(SyntaxLanguage language, const StyleDefinition& definition) const {
    if (wxString::FromUTF8(definition.key) == "Default") return Scheme(language).defaultStyle;
    const auto& styles = Scheme(language).tokenStyles;
    if (auto found = styles.find(wxString::FromUTF8(definition.key)); found != styles.end()) return found->second;
    return wxString::FromUTF8(definition.defaultValue);
}

void ApplyStyleProfile(EditorControl& editor, SyntaxLanguage language) {
    const auto& profile = editor.Fonts();
    const auto global = [&](const char* key) { return profile.GlobalStyle(key); };
    const auto baseFont = DefaultStyleFont(profile, profile.Scheme(language).useCode);
    const int baseSize =
        static_cast<int>(std::lround(baseFont.GetFractionalPointSize() * Scintilla::FontSizeMultiplier));
    const auto foreground = static_cast<unsigned>(editor.Send(StyleGetFore, DefaultStyle));
    const auto background = static_cast<unsigned>(editor.Send(StyleGetBack, DefaultStyle));
    for (const auto& definition : GlobalStyleDefinitions()) {
        if (wxString::FromUTF8(definition.key).StartsWith("Default ")) continue;
        const auto value = global(definition.key);
        for (size_t index = 0; index < definition.count; ++index)
            ApplyStyleText(editor, definition.ids[index], value, baseSize);
    }
    Indicator(editor, EditorIndicator::BraceMatch, global("Matching Brace"), 0x000aea02, 50, 200);
    Indicator(editor, EditorIndicator::BraceError, global("Matching Brace Error"), 0x000000ff, 50, 200);
    Indicator(editor, EditorIndicator::Occurrence, global("Mark Occurrences"), SystemColour(wxSYS_COLOUR_HIGHLIGHT), 25,
              150);
    editor.Send(BraceHighlightIndicator, 1, EditorIndicator::BraceMatch);
    editor.Send(BraceBadLightIndicator, 1, EditorIndicator::BraceError);
    const auto selection = global("Selected Text");
    const auto selectionBack = StyleColour(selection, "back", SystemColour(wxSYS_COLOUR_HIGHLIGHT));
    const auto selectionAlpha = std::clamp(StyleInteger(selection, "alpha", 95), 0, 255);
    editor.Send(SetSelectionLayer, std::to_underlying(Scintilla::Layer::OverText));
    for (const auto element : {Scintilla::Element::SelectionText, Scintilla::Element::SelectionAdditionalText,
                               Scintilla::Element::SelectionSecondaryText, Scintilla::Element::SelectionInactiveText})
        editor.Send(ResetElementColour, std::to_underlying(element));
    editor.Send(SetElementColour, std::to_underlying(Scintilla::Element::SelectionBack),
                selectionBack | (static_cast<unsigned>(selectionAlpha) << 24U));
    const auto additionalBack = StyleColour(selection, "fore", selectionBack);
    const auto additionalAlpha = std::clamp(StyleInteger(selection, "outline", selectionAlpha), 0, 255);
    for (const auto element : {Scintilla::Element::SelectionAdditionalBack, Scintilla::Element::SelectionSecondaryBack,
                               Scintilla::Element::SelectionInactiveBack})
        editor.Send(SetElementColour, std::to_underlying(element),
                    additionalBack | (static_cast<unsigned>(additionalAlpha) << 24U));
    editor.Send(SetSelEOLFilled, StyleAttribute(selection, "eolfilled"));
    editor.Send(SetEOLSelectedWidth, StyleInteger(selection, "size", 100));
    editor.Send(SetElementColour, std::to_underlying(Scintilla::Element::List), 0xff000000U | foreground);
    editor.Send(SetElementColour, std::to_underlying(Scintilla::Element::ListBack), 0xff000000U | background);
    editor.Send(SetElementColour, std::to_underlying(Scintilla::Element::ListSelectedBack),
                0xff000000U | selectionBack);
    const auto caret = global("Caret");
    editor.Send(SetElementColour, std::to_underlying(Scintilla::Element::Caret),
                0xff000000U | StyleColour(caret, "fore", foreground));
    editor.Send(SetElementColour, std::to_underlying(Scintilla::Element::CaretAdditional),
                0xff000000U | StyleColour(caret, "back", foreground));
    const auto whitespace = global("Whitespace");
    for (const auto& [element, key] :
         {std::pair{Scintilla::Element::WhiteSpace, "fore"}, std::pair{Scintilla::Element::WhiteSpaceBack, "back"}}) {
        if (StyleProperty(whitespace, key).empty())
            editor.Send(ResetElementColour, std::to_underlying(element));
        else
            editor.Send(SetElementColour, std::to_underlying(element),
                        StyleColour(whitespace, key, foreground) |
                            (static_cast<unsigned>(std::clamp(StyleInteger(whitespace, "alpha", 255), 0, 255)) << 24U));
    }
    const auto ime = StyleColour(global("IME Indicator"), "fore", 0x00108010);
    for (int id = std::to_underlying(Scintilla::IndicatorNumbers::Ime);
         id <= std::to_underlying(Scintilla::IndicatorNumbers::ImeMax); ++id)
        editor.Send(IndicSetFore, id, ime);
    const auto folding = global("Code Folding");
    editor.Send(SetFoldMarginColour, 1, StyleColour(folding, "back", background));
    editor.Send(SetFoldMarginHiColour, 1, StyleColour(folding, "back", background));
    const auto marker = global("Folding Marker");
    const auto markerLine = StyleColour(marker, "fore", 0x00ff8080);
    const auto markerFill = StyleColour(marker, "back", 0x00e6d8ad);
    for (auto id = std::to_underlying(Scintilla::MarkerOutline::FolderEnd);
         id <= std::to_underlying(Scintilla::MarkerOutline::FolderOpen); ++id) {
        editor.Send(MarkerSetForeTranslucent, id, 0xff000000U | background);
        editor.Send(MarkerSetBackTranslucent, id, 0xff000000U | markerLine);
        editor.Send(MarkerSetBackSelectedTranslucent, id, 0xff000000U | StyleColour(folding, "fore", 0x000000ff));
    }
    for (const auto id : {Scintilla::MarkerOutline::Folder, Scintilla::MarkerOutline::FolderEnd})
        editor.Send(MarkerSetForeTranslucent, std::to_underlying(id), 0xff000000U | markerFill);
    editor.Send(CallTipSetBack, editor.Send(StyleGetBack, std::to_underlying(Scintilla::StylesCommon::CallTip)));
    editor.Send(CallTipSetFore, editor.Send(StyleGetFore, std::to_underlying(Scintilla::StylesCommon::CallTip)));
    InstallTokens(editor, language, baseSize);
    const auto lexer = SyntaxFor(language).lexer;
    if (lexer == SCLEX_MARKDOWN) {
        for (const auto id : {SCE_MARKDOWN_PLAIN_LINK, SCE_MARKDOWN_PAREN_LINK, SCE_MARKDOWN_ANGLE_LINK})
            ApplyStyleText(editor, id, global("Link"), baseSize);
        InstallTokens(editor, SyntaxLanguage::Html, baseSize, 0, SCE_H_QUESTION);
    }
    if (lexer == SCLEX_HTML) {
        InstallTokens(editor, SyntaxLanguage::JavaScript, baseSize, SCE_STYLE_OFFSET_JS);
        InstallTokens(editor, SyntaxLanguage::Css, baseSize, SCE_STYLE_OFFSET_CSS);
    }
    const auto history = global("Change History Marker");
    for (const auto id :
         {Scintilla::MarkerOutline::HistoryRevertedToOrigin, Scintilla::MarkerOutline::HistorySaved,
          Scintilla::MarkerOutline::HistoryModified, Scintilla::MarkerOutline::HistoryRevertedToModified}) {
        const auto colour = StyleColour(history, id <= Scintilla::MarkerOutline::HistorySaved ? "fore" : "back",
                                        id <= Scintilla::MarkerOutline::HistorySaved ? 0x00008000 : 0x00ff8000);
        editor.Send(MarkerSetForeTranslucent, std::to_underlying(id), 0xff000000U | colour);
        editor.Send(MarkerSetBackTranslucent, std::to_underlying(id),
                    0xff000000U | (id == Scintilla::MarkerOutline::HistoryRevertedToOrigin ||
                                           id == Scintilla::MarkerOutline::HistoryRevertedToModified
                                       ? static_cast<unsigned>(editor.Send(
                                             StyleGetBack, std::to_underlying(Scintilla::StylesCommon::LineNumber)))
                                       : colour));
    }
    editor.SetBookmarkMarginVisible(editor.BookmarkMarginVisible());
    editor.ApplyStylePixelSettings();
    ApplyLongLineColour(editor);
}
