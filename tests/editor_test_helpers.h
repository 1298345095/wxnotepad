#pragma once

#include "editor/editor_control.h"
#include "app/editor_frame.h"
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
} // namespace TestEditor
