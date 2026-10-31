#include "syntax_scheme.h"
#include "editor_control.h"
#include <SciLexer.h>
#include <wx/filename.h>

namespace {
struct SyntaxStyle {
    int id;
    unsigned int foreground;
    unsigned int background;
    bool bold;
};
#include "cpp_scheme_data.h"

// Scintilla's public color values are BGR; upstream default strings are RGB.
constexpr unsigned int Colour(unsigned int rgb) {
    return ((rgb & 0xffU) << 16U) | (rgb & 0xff00U) | ((rgb >> 16U) & 0xffU);
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
    return SyntaxLanguage::PlainText;
}

wxString LanguageName(SyntaxLanguage language) {
    return language == SyntaxLanguage::Cpp ? "C/C++" : "Plain Text";
}

void ApplySyntaxLanguage(EditorControl& editor, SyntaxLanguage language) {
    using Scintilla::Message;
    editor.Send(Message::SetLexer, language == SyntaxLanguage::Cpp ? SCLEX_CPP : SCLEX_NULL);
    // Keep the native font/point size already installed on STYLE_DEFAULT.
    editor.Send(Message::StyleSetFore, 32, Colour(0x000000));
    editor.Send(Message::StyleSetBack, 32, Colour(0xFFFFFF));
    editor.Send(Message::StyleClearAll);
    if (language == SyntaxLanguage::Cpp) {
        editor.Send(Message::SetProperty, reinterpret_cast<uintptr_t>("lexer.lang"), reinterpret_cast<intptr_t>("0"));
        for (size_t index = 0; index < std::size(cppKeywords); ++index) {
            editor.Send(Message::SetKeyWords, index, reinterpret_cast<intptr_t>(cppKeywords[index]));
        }
        for (const auto& style : cppStyles) {
            editor.Send(Message::StyleSetFore, style.id, Colour(style.foreground));
            editor.Send(Message::StyleSetBack, style.id, Colour(style.background));
            editor.Send(Message::StyleSetBold, style.id, style.bold);
        }
    }
    editor.Send(Message::ClearDocumentStyle);
}
