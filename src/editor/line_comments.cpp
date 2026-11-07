#include "editor/editor_control.h"
#include <algorithm>
#include <cstring>
#include <string>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.

bool EditorControl::ToggleLineComments(const char* prefix, bool spaceAfterComment) {
    using Scintilla::Message;
    if (Send(Message::SelectionIsRectangle)) return false;

    // Adapted from Notepad4 89b8b36 Edit.cpp::EditToggleLineComments.
    // The delivered schemes supply ASCII prefixes without a mandatory pad.
    const auto selectionStart = Send(Message::GetSelectionStart);
    const auto selectionEnd = Send(Message::GetSelectionEnd);
    const auto current = Send(Message::GetCurrentPos);
    const auto prefixLength = static_cast<intptr_t>(std::strlen(prefix));
    const auto firstLine = Send(Message::LineFromPosition, selectionStart);
    auto lastLine = Send(Message::LineFromPosition, selectionEnd);
    if (selectionEnd <= Send(Message::PositionFromLine, lastLine) && lastLine > firstLine) --lastLine;

    intptr_t commentColumn = 1023 - prefixLength;
    for (auto line = firstLine; line <= lastLine; ++line) {
        const auto indent = Send(Message::GetLineIndentPosition, line);
        if (indent != Send(Message::GetLineEndPosition, line))
            commentColumn = std::min(commentColumn, Send(Message::GetColumn, indent, line));
    }

    enum class Action { None, Add, Delete };
    auto action = Action::None;
    const std::string padded = std::string(prefix) + (spaceAfterComment ? " " : "");
    Send(Message::BeginUndoAction);
    for (auto line = firstLine; line <= lastLine; ++line) {
        const auto indent = Send(Message::GetLineIndentPosition, line);
        const auto end = Send(Message::GetLineEndPosition, line);
        const auto length = std::min(prefixLength + 1, end - indent);
        const auto* bytes = reinterpret_cast<const char*>(Send(Message::GetRangePointer, indent, length));
        const bool commented = length >= prefixLength && std::memcmp(bytes, prefix, prefixLength) == 0;
        if (commented) {
            if (action == Action::None) action = Action::Delete;
            if (action == Action::Delete) {
                auto commentStart = indent;
                auto commentEnd = indent + prefixLength;
                if (spaceAfterComment && length > prefixLength && bytes[prefixLength] == ' ') ++commentEnd;
                if (commentEnd == end) commentStart = Send(Message::PositionFromLine, line);
                Send(Message::DeleteRange, commentStart, commentEnd - commentStart);
            } else {
                const auto position = Send(Message::FindColumn, line, commentColumn);
                if (position != indent) Send(Message::InsertText, position, reinterpret_cast<intptr_t>(padded.c_str()));
            }
        } else {
            if (action == Action::None) action = Action::Add;
            if (action == Action::Add) {
                const auto position = Send(Message::FindColumn, line, commentColumn);
                if (commentColumn == 0 || firstLine == lastLine || indent != end) {
                    const char* comment = position == end ? prefix : padded.c_str();
                    Send(Message::InsertText, position, reinterpret_cast<intptr_t>(comment));
                } else {
                    intptr_t tabs = 0;
                    auto spaces = commentColumn;
                    if (Send(Message::GetUseTabs)) {
                        const auto tabWidth = Send(Message::GetTabWidth);
                        tabs = spaces / tabWidth;
                        spaces %= tabWidth;
                    }
                    const std::string comment = std::string(tabs, '\t') + std::string(spaces, ' ') + prefix;
                    Send(Message::InsertText, position, reinterpret_cast<intptr_t>(comment.c_str()));
                }
            }
        }
    }
    Send(Message::EndUndoAction);
    if (selectionStart != selectionEnd) {
        const auto start = Send(Message::PositionFromLine, firstLine);
        const auto end = Send(Message::PositionFromLine, lastLine + 1);
        Send(Message::SetSel, current == selectionStart ? end : start, current == selectionStart ? start : end);
    }
    return true;
}
