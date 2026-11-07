#include "syntax/syntax_scheme.h"
#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <span>
#include <utility>
#include <wx/settings.h>

namespace {
// Scintilla's public color values are BGR; upstream default strings are RGB.
constexpr unsigned int Colour(unsigned int rgb) {
    return ((rgb & 0xffU) << 16U) | (rgb & 0xff00U) | ((rgb >> 16U) & 0xffU);
}

void InstallStyles(EditorControl& editor, std::span<const SyntaxStyle> styles) {
    using Scintilla::Message;
    for (const auto& style : styles) {
        editor.Send(Message::StyleSetFore, style.id, Colour(style.foreground));
        editor.Send(Message::StyleSetBack, style.id, Colour(style.background));
        editor.Send(Message::StyleSetBold, style.id, style.bold);
    }
}
} // namespace

void ApplyIndentationGuides(EditorControl& editor, SyntaxLanguage language, bool show, bool simple) {
    // Notepad4 89b8b36 Styles.cpp::Style_SetIndentGuides.
    const auto mode = !show                                   ? Scintilla::IndentView::None
                      : simple                                ? Scintilla::IndentView::Real
                      : SyntaxFor(language).indentLookForward ? Scintilla::IndentView::LookForward
                                                              : Scintilla::IndentView::LookBoth;
    editor.Send(Scintilla::Message::SetIndentationGuides, static_cast<uintptr_t>(mode));
}

void ApplyLongLineColour(EditorControl& editor) {
    // The original default style supplies a line foreground but no background.
    const auto background = wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT);
    const auto colour = editor.Send(Scintilla::Message::GetEdgeMode) == 1
                            ? Colour(0xFFC000)
                            : background.Red() | (background.Green() << 8U) | (background.Blue() << 16U);
    editor.Send(Scintilla::Message::SetEdgeColour, colour);
}

void ApplySyntaxLanguage(EditorControl& editor, SyntaxLanguage language) {
    using Scintilla::Message;
    const auto& definition = SyntaxFor(language);
    const bool fold = definition.canFold;
    editor.Send(Message::SetMarginTypeN, 2, static_cast<intptr_t>(Scintilla::MarginType::Symbol));
    editor.Send(Message::SetMarginMaskN, 2, Scintilla::MaskFolders);
    editor.Send(Message::SetMarginSensitiveN, 2, true);
    editor.SetCodeFoldingVisible(editor.CodeFoldingVisible());
    constexpr auto automatic = static_cast<uintptr_t>(Scintilla::AutomaticFold::Show) |
                               static_cast<uintptr_t>(Scintilla::AutomaticFold::Click) |
                               static_cast<uintptr_t>(Scintilla::AutomaticFold::Change);
    editor.Send(Message::SetAutomaticFold, fold ? automatic : 0);
    editor.Send(Message::SetFoldMarginColour, 1, Colour(0xFFFFFF));
    editor.Send(Message::SetFoldMarginHiColour, 1, Colour(0xFFFFFF));
    const Scintilla::MarkerSymbol folders[] = {Scintilla::MarkerSymbol::BoxPlusConnected,
                                               Scintilla::MarkerSymbol::BoxMinusConnected,
                                               Scintilla::MarkerSymbol::TCorner,
                                               Scintilla::MarkerSymbol::LCorner,
                                               Scintilla::MarkerSymbol::VLine,
                                               Scintilla::MarkerSymbol::BoxPlus,
                                               Scintilla::MarkerSymbol::BoxMinus};
    for (size_t index = 0; index < std::size(folders); ++index) {
        const size_t marker = 25 + index;
        editor.Send(Message::MarkerDefine, marker, static_cast<intptr_t>(folders[index]));
        editor.Send(Message::MarkerSetForeTranslucent, marker,
                    0xFF000000U | Colour(marker == 25 || marker == 30 ? 0xADD8E6 : 0xFFFFFF));
        editor.Send(Message::MarkerSetBackTranslucent, marker, 0xFF000000U | Colour(0x8080FF));
        editor.Send(Message::MarkerSetBackSelectedTranslucent, marker, 0xFF000000U | Colour(0xFF0000));
        editor.Send(Message::MarkerSetStrokeWidth, marker, editor.FromDIP(100));
    }
    editor.Send(Message::SetLexer, definition.lexer);
    editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>("fold"),
                reinterpret_cast<intptr_t>(fold ? "1" : "0"));
    // Keep the native font/point size already installed on STYLE_DEFAULT.
    editor.Send(Message::StyleSetFore, 32, Colour(0x000000));
    editor.Send(Message::StyleSetBack, 32, Colour(0xFFFFFF));
    editor.Send(Message::StyleClearAll);
    // Original stlDefault.cpp / Styles.cpp: brace feedback uses independent
    // translucent overlays, preserving the current font and DPI settings.
    editor.Send(Message::BraceHighlightIndicator, 1, 9);
    editor.Send(Message::BraceBadLightIndicator, 1, 10);
    for (const auto [indicator, colour] : {std::pair{9, 0x02EA0A}, std::pair{10, 0xFF0000}}) {
        editor.Send(Message::IndicSetStyle, indicator, static_cast<intptr_t>(Scintilla::IndicatorStyle::RoundBox));
        editor.Send(Message::IndicSetFore, indicator, Colour(colour));
        editor.Send(Message::IndicSetAlpha, indicator, 50);
        editor.Send(Message::IndicSetOutlineAlpha, indicator, 200);
    }
    for (const auto& property : definition.properties)
        editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>(property.name),
                    reinterpret_cast<intptr_t>(property.value));
    for (size_t index = 0; index < definition.keywords.size(); ++index) {
        const auto& list = definition.keywords[index];
        editor.Send(Message::SetKeyWords, index | list.attributes, reinterpret_cast<intptr_t>(list.text));
    }
    InstallStyles(editor, definition.styles);
    editor.Send(Message::StyleSetFore, 37, Colour(0xFF8000));
    ApplyLongLineColour(editor);
    const auto guides = editor.Send(Message::GetIndentationGuides);
    if (guides != 0) ApplyIndentationGuides(editor, language, true, guides == 1);
    editor.SetCurrentLineHighlightMode(editor.GetCurrentLineHighlightMode());
    editor.Send(Message::ClearDocumentStyle);
}
