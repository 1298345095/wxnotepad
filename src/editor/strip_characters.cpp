#include "editor/editor_control.h"

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.

bool EditorControl::StripFirstCharacter() {
    using Scintilla::Message;
    // Notepad4 89b8b36 Edit.cpp::EditStripFirstCharacter.
    if (Send(Message::SelectionIsRectangle)) return false;
    const auto start = Send(Message::GetSelectionStart);
    const auto end = Send(Message::GetSelectionEnd);
    auto firstLine = Send(Message::LineFromPosition, start);
    auto lastLine = Send(Message::LineFromPosition, end);
    if (firstLine != lastLine) {
        if (start > Send(Message::PositionFromLine, firstLine)) ++firstLine;
        if (end <= Send(Message::PositionFromLine, lastLine)) --lastLine;
    }
    Send(Message::BeginUndoAction);
    for (auto line = firstLine; line <= lastLine; ++line) {
        const auto position = Send(Message::PositionFromLine, line);
        if (Send(Message::GetLineEndPosition, line) > position)
            Send(Message::DeleteRange, position, Send(Message::PositionAfter, position) - position);
    }
    Send(Message::EndUndoAction);
    return true;
}

bool EditorControl::StripLastCharacter() {
    using Scintilla::Message;
    // Notepad4 89b8b36 Edit.cpp::EditStripLastCharacter.
    if (Send(Message::SelectionIsRectangle)) return false;
    const auto start = Send(Message::GetSelectionStart);
    const auto end = Send(Message::GetSelectionEnd);
    auto firstLine = Send(Message::LineFromPosition, start);
    auto lastLine = Send(Message::LineFromPosition, end);
    if (firstLine != lastLine) {
        if (start >= Send(Message::GetLineEndPosition, firstLine)) ++firstLine;
        if (end < Send(Message::GetLineEndPosition, lastLine)) --lastLine;
    }
    Send(Message::BeginUndoAction);
    for (auto line = firstLine; line <= lastLine; ++line) {
        const auto lineStart = Send(Message::PositionFromLine, line);
        const auto lineEnd = Send(Message::GetLineEndPosition, line);
        if (lineEnd > lineStart) {
            const auto position = Send(Message::PositionBefore, lineEnd);
            Send(Message::DeleteRange, position, lineEnd - position);
        }
    }
    Send(Message::EndUndoAction);
    return true;
}
