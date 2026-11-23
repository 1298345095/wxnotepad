#include "syntax/syntax_scheme.h"
#include "editor/editor_ids.h"
#include "editor/editor_control.h"
#include "editor/font_settings.h"
#include <ScintillaTypes.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <span>
#include <SciLexer.h>
#include <utility>
#include <wx/settings.h>

namespace {
// Scintilla's public color values are BGR; upstream default strings are RGB.
constexpr unsigned int Colour(unsigned int rgb) {
    return ((rgb & 0xffU) << 16U) | (rgb & 0xff00U) | ((rgb >> 16U) & 0xffU);
}

void InstallFolding(EditorControl& editor, bool fold) {
    using enum Scintilla::MarkerSymbol;
    using enum Scintilla::Message;
    editor.Send(SetMarginTypeN, EditorMargin::Folding,
                static_cast<intptr_t>(std::to_underlying(Scintilla::MarginType::Symbol)));
    editor.Send(SetMarginMaskN, EditorMargin::Folding, Scintilla::MaskFolders);
    editor.Send(SetMarginSensitiveN, EditorMargin::Folding, true);
    editor.SetCodeFoldingVisible(editor.CodeFoldingVisible());
    constexpr auto automatic = static_cast<uintptr_t>(std::to_underlying(Scintilla::AutomaticFold::Show)) |
                               static_cast<uintptr_t>(std::to_underlying(Scintilla::AutomaticFold::Click)) |
                               static_cast<uintptr_t>(std::to_underlying(Scintilla::AutomaticFold::Change));
    editor.Send(SetAutomaticFold, fold ? automatic : 0);
    // Retain Scintilla/wxSTC's native chrome pattern instead of forcing a
    // white strip beside the native line-number margin in Dark/High Contrast.
    editor.Send(SetFoldMarginColour, 0);
    editor.Send(SetFoldMarginHiColour, 0);
    const std::array<Scintilla::MarkerSymbol, 7> folders = {
        BoxPlusConnected, BoxMinusConnected, TCorner, LCorner, VLine, BoxPlus, BoxMinus};
    for (size_t index = 0; index < std::size(folders); ++index) {
        using enum Scintilla::MarkerOutline;
        const size_t marker = static_cast<size_t>(std::to_underlying(FolderEnd)) + index;
        editor.Send(MarkerDefine, marker, static_cast<intptr_t>(std::to_underlying(folders[index])));
        editor.Send(MarkerSetForeTranslucent, marker,
                    0xFF000000U | Colour(marker == static_cast<size_t>(std::to_underlying(FolderEnd)) ||
                                                 marker == static_cast<size_t>(std::to_underlying(Folder))
                                             ? 0xADD8E6
                                             : 0xFFFFFF));
        editor.Send(MarkerSetBackTranslucent, marker, 0xFF000000U | Colour(0x8080FF));
        editor.Send(MarkerSetBackSelectedTranslucent, marker, 0xFF000000U | Colour(0xFF0000));
        editor.Send(MarkerSetStrokeWidth, marker, editor.FromDIP(100));
    }
}
} // namespace

void ApplyIndentationGuides(EditorControl& editor, SyntaxLanguage language, bool show, bool simple) {
    using enum Scintilla::IndentView;
    // Notepad4 89b8b36 Styles.cpp::Style_SetIndentGuides.
    auto mode = None;
    if (show) {
        if (simple)
            mode = Real;
        else
            mode = SyntaxFor(language).indentLookForward ? LookForward : LookBoth;
    }
    editor.Send(Scintilla::Message::SetIndentationGuides, static_cast<uintptr_t>(std::to_underlying(mode)));
}

void ApplyLongLineColour(EditorControl& editor) {
    // The original default style supplies a line foreground but no background.
    const auto background = wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT);
    const auto colour = StyleColour(editor.Fonts().GlobalStyle("Long Line Marker"),
                                    editor.Send(Scintilla::Message::GetEdgeMode) == 1 ? "fore" : "back",
                                    editor.Send(Scintilla::Message::GetEdgeMode) == 1
                                        ? Colour(0xFFC000)
                                        : background.Red() | (background.Green() << 8U) | (background.Blue() << 16U));
    editor.Send(Scintilla::Message::SetEdgeColour, colour);
}

void ApplySyntaxStyles(EditorControl& editor, SyntaxLanguage language) {
    ApplyFontSettings(editor, language);
    editor.Send(Scintilla::Message::StyleClearAll);
    ApplyStyleProfile(editor, language);
}

void ApplySyntaxLanguage(EditorControl& editor, SyntaxLanguage language, std::optional<SyntaxLanguage> markupDialect) {
    using Scintilla::Message;
    const auto& definition = SyntaxFor(language);
    editor.SetWordCompletionHandler([&editor, language](WordCompletionCondition condition) {
        CompleteWord(editor, SyntaxFor(language), editor.GetCompletionSettings(), false, condition);
    });
    editor.SetCompletionStyles(CompletionStylesFor(definition));
    const bool fold = definition.canFold;
    InstallFolding(editor, fold);
    editor.Send(Message::SetLexer, definition.lexer);
    // A shared lexer can retain settings when switching dialects. Reset the
    // delivered property and keyword slots before installing the next scheme.
    editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>("lexer.lang"), reinterpret_cast<intptr_t>("0"));
    for (int index = 0; index <= Scintilla::KeywordsetMax; ++index)
        editor.Send(Message::SetKeyWords, index, reinterpret_cast<intptr_t>(""));
    editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>("fold"),
                reinterpret_cast<intptr_t>(fold ? "1" : "0"));
    // Style_SetLexer keeps the selected family even when a canonical menu
    // choice sniffs a dialect from the other family. Only HTML dialects alter
    // HTML options; XML keeps its own options for a conflicting ASP/HTML header.
    const auto propertyLanguage =
        markupDialect && SyntaxSettingsLanguage(language) == SyntaxLanguage::Html
            ? (SyntaxSettingsLanguage(*markupDialect) == SyntaxLanguage::Html ? *markupDialect : SyntaxLanguage::Html)
            : language;
    for (const auto& property : SyntaxFor(propertyLanguage).properties)
        editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>(property.name),
                    reinterpret_cast<intptr_t>(property.value));
    for (size_t index = 0; index < definition.keywords.size(); ++index) {
        const auto& list = definition.keywords[index];
        if (list.attributes & SyntaxKeywordAttribute::NoLexer) continue;
        editor.Send(Message::SetKeyWords, index | list.attributes, reinterpret_cast<intptr_t>(list.text));
    }
    ApplySyntaxStyles(editor, language);
    const auto guides = editor.Send(Message::GetIndentationGuides);
    if (guides != 0) ApplyIndentationGuides(editor, language, true, guides == 1);
    editor.SetCurrentLineHighlightMode(editor.GetCurrentLineHighlightMode());
    editor.Send(Message::ClearDocumentStyle);
}
