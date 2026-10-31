#include "editor/line_navigation.h"
#include "editor/editor_control.h"
#include <charconv>

bool GoToLogicalLine(EditorControl& editor, const wxString& input) {
    wxString trimmed(input);
    trimmed.Trim().Trim(false);
    const auto bytes = trimmed.utf8_str();
    uintptr_t number = 0;
    const auto parsed = std::from_chars(bytes.data(), bytes.data() + bytes.length(), number);
    using Scintilla::Message;
    if (parsed.ec != std::errc{} || parsed.ptr != bytes.data() + bytes.length() || number == 0 ||
        number > static_cast<uintptr_t>(editor.Send(Message::GetLineCount)))
        return false;
    const auto line = number - 1;
    // Like the original EditSelectEx, reveal before moving the caret.
    editor.Send(Message::EnsureVisible, line);
    editor.Send(Message::GotoPos, editor.Send(Message::PositionFromLine, line));
    editor.Send(Message::ChooseCaretX);
    editor.Send(Message::ScrollCaret);
    return true;
}
