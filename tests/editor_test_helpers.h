#pragma once

#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include <cstdio>
#include <optional>
#include <string>
#include <stdexcept>

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
    auto* editor = frame->Editor();
    editor->Send(Scintilla::Message::GotoPos, editor->Send(Scintilla::Message::GetLength));
    editor->Send(Scintilla::Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
}
inline void ResetStream(EditorControl* editor, const char* text, intptr_t anchor = 0, intptr_t caret = 0,
                        std::optional<Scintilla::EndOfLine> mode = std::nullopt) {
    using Scintilla::Message;
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    if (mode) editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(*mode));
    editor->Send(Message::SetSel, anchor, caret);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
template <class Command>
void RequireVirtualRectangleUnchanged(EditorControl* editor, Command command, const char* message) {
    using Scintilla::Message;
    ResetStream(editor, "a\nx");
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 3);
    editor->Send(Message::SetRectangularSelectionAnchorVirtualSpace, 1);
    editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, 3);
    command();
    if (Text(editor) != "a\nx" || editor->Send(Message::CanUndo) || editor->Send(Message::GetModify) ||
        editor->Send(Message::GetRectangularSelectionAnchorVirtualSpace) != 1 ||
        editor->Send(Message::GetRectangularSelectionCaretVirtualSpace) != 3)
        throw Failure(message);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    editor->Send(Message::SetMultipleSelection, 0);
}
inline void RequireSelection(EditorControl* editor, intptr_t anchor, intptr_t caret, const char* message) {
    using Scintilla::Message;
    if (editor->Send(Message::GetAnchor) != anchor || editor->Send(Message::GetCurrentPos) != caret) {
        std::fprintf(stderr, "Selection anchor/caret: %lld/%lld; expected %lld/%lld\n",
                     static_cast<long long>(editor->Send(Message::GetAnchor)),
                     static_cast<long long>(editor->Send(Message::GetCurrentPos)), static_cast<long long>(anchor),
                     static_cast<long long>(caret));
        throw std::runtime_error(message);
    }
}
} // namespace TestEditor
