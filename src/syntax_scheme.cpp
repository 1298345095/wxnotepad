#include "syntax_scheme.h"
#include "editor_control.h"
#include <SciLexer.h>
#include <ScintillaTypes.h>
#include <span>
#include <wx/filename.h>

namespace {
struct SyntaxStyle {
    int id;
    unsigned int foreground;
    unsigned int background;
    bool bold;
};
#include "cpp_scheme_data.h"
#include "json_scheme_data.h"
#include "python_scheme_data.h"

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

SyntaxLanguage LanguageForPath(const wxString& path) {
    wxString extension = wxFileName(path).GetExt();
    for (size_t index = 0; index < extension.length(); ++index) {
        const auto ch = extension[index].GetValue();
        if (ch >= 'A' && ch <= 'Z') extension[index] = ch + ('a' - 'A');
    }
    for (const auto* candidate : {"c", "cpp", "cxx", "cc", "h", "hpp", "hxx", "hh"}) {
        if (extension == candidate) return SyntaxLanguage::Cpp;
    }
    if (extension == "json") return SyntaxLanguage::Json;
    for (const auto* candidate : {"py", "pyw", "pyi"}) {
        if (extension == candidate) return SyntaxLanguage::Python;
    }
    return SyntaxLanguage::PlainText;
}

wxString LanguageName(SyntaxLanguage language) {
    if (language == SyntaxLanguage::Cpp) return "C/C++";
    if (language == SyntaxLanguage::Json) return "JSON";
    if (language == SyntaxLanguage::Python) return "Python";
    return "Plain Text";
}

bool LanguageCanFold(SyntaxLanguage language) {
    return language == SyntaxLanguage::Cpp || language == SyntaxLanguage::Python || language == SyntaxLanguage::Json;
}

void ApplySyntaxLanguage(EditorControl& editor, SyntaxLanguage language) {
    using Scintilla::Message;
    const bool fold = LanguageCanFold(language);
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
    editor.Send(Message::SetLexer, language == SyntaxLanguage::Cpp      ? SCLEX_CPP
                                   : language == SyntaxLanguage::Python ? SCLEX_PYTHON
                                   : language == SyntaxLanguage::Json   ? SCLEX_JSON
                                                                        : SCLEX_NULL);
    editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>("fold"),
                reinterpret_cast<intptr_t>(fold ? "1" : "0"));
    // Keep the native font/point size already installed on STYLE_DEFAULT.
    editor.Send(Message::StyleSetFore, 32, Colour(0x000000));
    editor.Send(Message::StyleSetBack, 32, Colour(0xFFFFFF));
    editor.Send(Message::StyleClearAll);
    if (language == SyntaxLanguage::Cpp) {
        editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>("lexer.lang"), reinterpret_cast<intptr_t>("0"));
        for (size_t index = 0; index < std::size(cppKeywords); ++index) {
            editor.Send(Message::SetKeyWords, index, reinterpret_cast<intptr_t>(cppKeywords[index]));
        }
        InstallStyles(editor, cppStyles);
    } else if (language == SyntaxLanguage::Json) {
        for (size_t index = 0; index < std::size(jsonKeywords); ++index) {
            editor.Send(Message::SetKeyWords, index | jsonKeywordAttributes,
                        reinterpret_cast<intptr_t>(jsonKeywords[index]));
        }
        InstallStyles(editor, jsonStyles);
    }
    if (language == SyntaxLanguage::Python) {
        // The pinned core encodes WordList::KeywordAttr_PreSorted above the index.
        for (size_t index = 0; index < std::size(pythonKeywords); ++index) {
            editor.Send(Message::SetKeyWords, index | (2U << 8U), reinterpret_cast<intptr_t>(pythonKeywords[index]));
        }
        InstallStyles(editor, pythonStyles);
    }
    editor.Send(Message::ClearDocumentStyle);
}
