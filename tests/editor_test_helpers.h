#pragma once

#include "editor/editor_control.h"
#include "editor/font_settings.h"
#include "app/editor_frame.h"
#include <cstdio>
#include <print>
#include <optional>
#include <string>
#include <stdexcept>
#include <utility>

namespace TestEditor {
class Failure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline std::string Text(EditorControl* editor) {
    std::string text(static_cast<size_t>(editor->Send(Scintilla::Message::GetLength)) + 1, '\0');
    editor->Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
inline std::string Text(EditorFrame* frame) {
    return Text(frame->Editor());
}
inline void Append(EditorFrame* frame, const std::string& text) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    editor->Send(GotoPos, editor->Send(GetLength));
    editor->Send(AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
}
// Frozen editing/geometry fixtures were captured with a fixed-width 12pt
// buffer. Keep that input explicit when the delivered Plain Text profile uses
// its independent text font; leave font-default acceptance to default_fonts.
inline void UseFixedWidthFont(EditorControl* editor, double points = 12) {
    auto fonts = editor->Fonts();
    fonts.codeStyle = wxString::Format("font:$(Code); size:%g", points);
    fonts.textStyle = fonts.codeStyle;
    editor->SetFontSettings(fonts);
    ApplyFontSettings(*editor, SyntaxLanguage::PlainText);
    editor->Send(Scintilla::Message::StyleClearAll);
}
inline void ResetStream(EditorControl* editor, const char* text, intptr_t anchor = 0, intptr_t caret = 0,
                        std::optional<Scintilla::EndOfLine> mode = std::nullopt) {
    using enum Scintilla::Message;
    editor->Send(SetReadOnly, 0);
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream)));
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(text));
    if (mode) editor->Send(SetEOLMode, static_cast<uintptr_t>(std::to_underlying(*mode)));
    editor->Send(SetSel, anchor, caret);
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
}
template <class Command>
void RequireVirtualRectangleUnchanged(EditorControl* editor, Command command, const char* message) {
    using enum Scintilla::Message;
    ResetStream(editor, "a\nx");
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Rectangle)));
    editor->Send(SetRectangularSelectionAnchor, 1);
    editor->Send(SetRectangularSelectionCaret, 3);
    editor->Send(SetRectangularSelectionAnchorVirtualSpace, 1);
    editor->Send(SetRectangularSelectionCaretVirtualSpace, 3);
    command();
    if (Text(editor) != "a\nx" || editor->Send(CanUndo) || editor->Send(GetModify) ||
        editor->Send(GetRectangularSelectionAnchorVirtualSpace) != 1 ||
        editor->Send(GetRectangularSelectionCaretVirtualSpace) != 3)
        throw Failure(message);
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream)));
    editor->Send(SetMultipleSelection, 0);
}
inline void RequireSelection(EditorControl* editor, intptr_t anchor, intptr_t caret, const char* message) {
    using enum Scintilla::Message;
    if (editor->Send(GetAnchor) != anchor || editor->Send(GetCurrentPos) != caret) {
        std::println(stderr, "Selection anchor/caret: {:d}/{:d}; expected {:d}/{:d}",
                     static_cast<long long>(editor->Send(GetAnchor)),
                     static_cast<long long>(editor->Send(GetCurrentPos)), static_cast<long long>(anchor),
                     static_cast<long long>(caret));
        throw Failure(message);
    }
}
} // namespace TestEditor
