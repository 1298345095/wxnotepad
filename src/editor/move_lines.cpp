#include "editor/editor_control.h"
#include <algorithm>

using Scintilla::Message;

void EditorControl::MoveLines(bool upwards) {
    if (Send(Message::GetReadOnly)) return;
    // Notepad4 Edit.cpp::EditMoveUp/EditMoveDown at 89b8b36.
    const auto caret = Send(Message::GetCurrentPos);
    const auto anchor = Send(Message::GetAnchor);
    const auto caretLine = Send(Message::LineFromPosition, caret);
    const auto anchorLine = Send(Message::LineFromPosition, anchor);
    if (caretLine == anchorLine) {
        const auto destination = caretLine + (upwards ? -1 : 1);
        if (destination < 0 || destination >= Send(Message::GetLineCount)) return;
        const auto start = Send(Message::PositionFromLine, caretLine);
        if (!upwards) Send(Message::GotoLine, destination);
        Send(Message::LineTranspose);
        const auto target = Send(Message::PositionFromLine, destination);
        Send(Message::SetSel, target + anchor - start, target + caret - start);
        Send(Message::ChooseCaretX);
        return;
    }
    auto first = std::min(caretLine, anchorLine);
    auto last = std::max(caretLine, anchorLine);
    if (std::max(caret, anchor) <= Send(Message::PositionFromLine, last)) --last;
    if (upwards) {
        if (first == 0) return;
        Send(Message::MoveSelectedLinesUp);
        --first;
    } else {
        if (last + 1 >= Send(Message::GetLineCount)) return;
        Send(Message::MoveSelectedLinesDown);
        ++first;
        last += 2;
    }
    const auto start = Send(Message::PositionFromLine, first);
    const auto end = Send(Message::PositionFromLine, last);
    Send(Message::SetSel, caret < anchor ? end : start, caret < anchor ? start : end);
}
