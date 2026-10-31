#include "syntax/syntax_scheme.h"
#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <span>

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

void ApplySyntaxLanguage(EditorControl& editor, SyntaxLanguage language) {
    using Scintilla::Message;
    const auto& definition = SyntaxFor(language);
    const bool fold = definition.canFold;
    editor.Send(Message::SetMarginTypeN, 1, static_cast<intptr_t>(Scintilla::MarginType::Symbol));
    editor.Send(Message::SetMarginMaskN, 1, Scintilla::MaskFolders);
    editor.Send(Message::SetMarginSensitiveN, 1, fold);
    editor.Send(Message::SetMarginWidthN, 1, fold ? editor.FromDIP(16) : 0);
    constexpr auto automatic = static_cast<uintptr_t>(Scintilla::AutomaticFold::Show) |
                               static_cast<uintptr_t>(Scintilla::AutomaticFold::Click) |
                               static_cast<uintptr_t>(Scintilla::AutomaticFold::Change);
    editor.Send(Message::SetAutomaticFold, fold ? automatic : 0);
    editor.Send(Message::SetFoldMarginColour, 1, Colour(0xF0F0F0));
    editor.Send(Message::SetFoldMarginHiColour, 1, Colour(0xF0F0F0));
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
        editor.Send(Message::MarkerSetForeTranslucent, marker, 0xFF000000U | Colour(0x808080));
        editor.Send(Message::MarkerSetBackTranslucent, marker, 0xFF000000U | Colour(0xFFFFFF));
        editor.Send(Message::MarkerSetStrokeWidth, marker, editor.FromDIP(100));
    }
    editor.Send(Message::SetLexer, definition.lexer);
    editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>("fold"),
                reinterpret_cast<intptr_t>(fold ? "1" : "0"));
    // Keep the native font/point size already installed on STYLE_DEFAULT.
    editor.Send(Message::StyleSetFore, 32, Colour(0x000000));
    editor.Send(Message::StyleSetBack, 32, Colour(0xFFFFFF));
    editor.Send(Message::StyleClearAll);
    for (const auto& property : definition.properties)
        editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>(property.name),
                    reinterpret_cast<intptr_t>(property.value));
    for (size_t index = 0; index < definition.keywords.size(); ++index) {
        const auto& list = definition.keywords[index];
        editor.Send(Message::SetKeyWords, index | list.attributes, reinterpret_cast<intptr_t>(list.text));
    }
    InstallStyles(editor, definition.styles);
    editor.Send(Message::ClearDocumentStyle);
}
