#include "editor/editor_control.h"

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.

bool EditorControl::RemoveBlankLines(bool merge) {
    using Scintilla::Message;
    // Adapted from Notepad4 89b8b36 Edit.cpp::EditRemoveBlankLines.
    if (Send(Message::SelectionIsRectangle)) return false;

    auto selectionStart = Send(Message::GetSelectionStart);
    auto selectionEnd = Send(Message::GetSelectionEnd);
    if (selectionStart == selectionEnd) {
        selectionStart = 0;
        selectionEnd = Send(Message::GetLength);
    }
    auto firstLine = Send(Message::LineFromPosition, selectionStart);
    auto lastLine = Send(Message::LineFromPosition, selectionEnd);
    if (selectionStart > Send(Message::PositionFromLine, firstLine)) ++firstLine;
    if (selectionEnd <= Send(Message::PositionFromLine, lastLine) && lastLine != Send(Message::GetLineCount) - 1)
        --lastLine;

    Send(Message::BeginUndoAction);
    for (auto line = firstLine; line <= lastLine;) {
        intptr_t blanks = 0;
        while (line + blanks <= lastLine &&
               Send(Message::GetLineIndentPosition, line + blanks) == Send(Message::GetLineEndPosition, line + blanks))
            ++blanks;
        if (blanks == 0 || (blanks == 1 && merge)) {
            line += blanks + 1;
        } else {
            if (merge) --blanks;
            const auto start = Send(Message::PositionFromLine, line);
            const auto end = Send(Message::PositionFromLine, line + blanks);
            Send(Message::DeleteRange, start, end - start);
            if (merge) ++line;
            lastLine -= blanks;
        }
    }
    Send(Message::EndUndoAction);
    return true;
}
