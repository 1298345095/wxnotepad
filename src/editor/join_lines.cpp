#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <string>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.

void EditorControl::JoinLines() {
    // Notepad4 89b8b36 Notepad4.cpp::IDM_EDIT_JOINLINES uses the main selection,
    // including rectangular mode, without expanding to whole lines.
    Send(Scintilla::Message::TargetFromSelection);
    Send(Scintilla::Message::LinesJoin);
}

bool EditorControl::JoinParagraphs() {
    using Scintilla::Message;
    if (Send(Message::GetSelectionEmpty)) return true;
    if (Send(Message::SelectionIsRectangle)) return false;

    // Adapted from Notepad4 89b8b36 Edit.cpp::EditJoinLinesEx.
    const auto start =
        Send(Message::PositionFromLine, Send(Message::LineFromPosition, Send(Message::GetSelectionStart)));
    const auto end = Send(Message::GetSelectionEnd);
    const auto length = end - start;
    const auto* bytes = reinterpret_cast<const char*>(Send(Message::GetRangePointer, start, length));
    const std::string text(bytes, static_cast<size_t>(length));
    const auto mode = static_cast<Scintilla::EndOfLine>(Send(Message::GetEOLMode));
    const char* eol = mode == Scintilla::EndOfLine::CrLf ? "\r\n" : mode == Scintilla::EndOfLine::Cr ? "\r" : "\n";
    const auto isEol = [](char ch) { return ch == '\r' || ch == '\n'; };
    std::string joined;
    joined.reserve(text.size());
    bool modified = false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (isEol(text[i])) {
            if (text[i] == '\r' && text[i + 1] == '\n') ++i;
            if (!isEol(text[i + 1]) && text[i + 1] != '\0') {
                joined += ' ';
                modified = true;
            } else {
                while (isEol(text[i + 1])) {
                    ++i;
                    modified = true;
                }
                if (text[i + 1] != '\0') {
                    if (!joined.empty()) joined += eol;
                    joined += eol;
                }
            }
        } else {
            joined += text[i];
        }
    }
    if (modified) {
        // Original EditReplaceRange replaces even identical bytes and repairs
        // the expanded selection in its original direction after the core call.
        const bool reversed = Send(Message::GetAnchor) > Send(Message::GetCurrentPos);
        Send(Message::SetTargetRange, start, end);
        Send(Message::ReplaceTarget, joined.size(), reinterpret_cast<intptr_t>(joined.data()));
        const auto joinedEnd = start + static_cast<intptr_t>(joined.size());
        Send(Message::SetSel, reversed ? joinedEnd : start, reversed ? start : joinedEnd);
    }
    return true;
}
