#include "syntax/syntax_scheme.h"
#include "editor/editor_ids.h"
#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <span>
#include <SciLexer.h>
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
        editor.Send(Message::StyleSetItalic, style.id, style.italic);
        editor.Send(Message::StyleSetEOLFilled, style.id, style.eolFilled);
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

void ApplySyntaxLanguage(EditorControl& editor, SyntaxLanguage language, std::optional<SyntaxLanguage> markupDialect) {
    using Scintilla::Message;
    const auto& definition = SyntaxFor(language);
    const bool fold = definition.canFold;
    editor.Send(Message::SetMarginTypeN, EditorMargin::Folding, static_cast<intptr_t>(Scintilla::MarginType::Symbol));
    editor.Send(Message::SetMarginMaskN, EditorMargin::Folding, Scintilla::MaskFolders);
    editor.Send(Message::SetMarginSensitiveN, EditorMargin::Folding, true);
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
        const size_t marker = static_cast<size_t>(Scintilla::MarkerOutline::FolderEnd) + index;
        editor.Send(Message::MarkerDefine, marker, static_cast<intptr_t>(folders[index]));
        editor.Send(Message::MarkerSetForeTranslucent, marker,
                    0xFF000000U | Colour(marker == static_cast<size_t>(Scintilla::MarkerOutline::FolderEnd) ||
                                                 marker == static_cast<size_t>(Scintilla::MarkerOutline::Folder)
                                             ? 0xADD8E6
                                             : 0xFFFFFF));
        editor.Send(Message::MarkerSetBackTranslucent, marker, 0xFF000000U | Colour(0x8080FF));
        editor.Send(Message::MarkerSetBackSelectedTranslucent, marker, 0xFF000000U | Colour(0xFF0000));
        editor.Send(Message::MarkerSetStrokeWidth, marker, editor.FromDIP(100));
    }
    editor.Send(Message::SetLexer, definition.lexer);
    // A shared lexer can retain settings when switching dialects. Reset the
    // delivered property and keyword slots before installing the next scheme.
    editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>("lexer.lang"), reinterpret_cast<intptr_t>("0"));
    for (int index = 0; index <= Scintilla::KeywordsetMax; ++index)
        editor.Send(Message::SetKeyWords, index, reinterpret_cast<intptr_t>(""));
    editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>("fold"),
                reinterpret_cast<intptr_t>(fold ? "1" : "0"));
    // Keep the native font/point size already installed on STYLE_DEFAULT.
    editor.Send(Message::StyleSetFore, static_cast<uintptr_t>(Scintilla::StylesCommon::Default), Colour(0x000000));
    editor.Send(Message::StyleSetBack, static_cast<uintptr_t>(Scintilla::StylesCommon::Default), Colour(0xFFFFFF));
    editor.Send(Message::StyleClearAll);
    // Original stlDefault.cpp / Styles.cpp: brace feedback uses independent
    // translucent overlays, preserving the current font and DPI settings.
    editor.Send(Message::BraceHighlightIndicator, 1, EditorIndicator::BraceMatch);
    editor.Send(Message::BraceBadLightIndicator, 1, EditorIndicator::BraceError);
    for (const auto [indicator, colour] :
         {std::pair{EditorIndicator::BraceMatch, 0x02EA0A}, std::pair{EditorIndicator::BraceError, 0xFF0000}}) {
        editor.Send(Message::IndicSetStyle, indicator, static_cast<intptr_t>(Scintilla::IndicatorStyle::RoundBox));
        editor.Send(Message::IndicSetFore, indicator, Colour(colour));
        editor.Send(Message::IndicSetAlpha, indicator, 50);
        editor.Send(Message::IndicSetOutlineAlpha, indicator, 200);
    }
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
        editor.Send(Message::SetKeyWords, index | list.attributes, reinterpret_cast<intptr_t>(list.text));
    }
    InstallStyles(editor, definition.styles);
    if (definition.lexer == SCLEX_HTML) InstallStyles(editor, MarkupEmbeddedStyles());
    editor.Send(Message::StyleSetFore, static_cast<uintptr_t>(Scintilla::StylesCommon::IndentGuide), Colour(0xFF8000));
    ApplyLongLineColour(editor);
    const auto guides = editor.Send(Message::GetIndentationGuides);
    if (guides != 0) ApplyIndentationGuides(editor, language, true, guides == 1);
    editor.SetCurrentLineHighlightMode(editor.GetCurrentLineHighlightMode());
    editor.Send(Message::ClearDocumentStyle);
}
