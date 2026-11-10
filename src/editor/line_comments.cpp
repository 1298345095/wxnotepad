#include "editor/editor_control.h"
#include <algorithm>
#include <cstring>
#include <string>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.

namespace {
using Scintilla::Message;
enum class CommentAction { None, Add, Delete };

void AddLineComment(EditorControl& editor, intptr_t line, intptr_t indent, intptr_t end, intptr_t column,
                    bool singleLine, const char* prefix, const std::string& padded) {
    const auto position = editor.Send(Message::FindColumn, line, column);
    if (column == 0 || singleLine || indent != end) {
        const char* comment = position == end ? prefix : padded.c_str();
        editor.Send(Message::InsertText, position, reinterpret_cast<intptr_t>(comment));
        return;
    }
    intptr_t tabs = 0;
    auto spaces = column;
    if (editor.Send(Message::GetUseTabs)) {
        const auto tabWidth = editor.Send(Message::GetTabWidth);
        tabs = spaces / tabWidth;
        spaces %= tabWidth;
    }
    const std::string comment = std::string(tabs, '\t') + std::string(spaces, ' ') + prefix;
    editor.Send(Message::InsertText, position, reinterpret_cast<intptr_t>(comment.c_str()));
}

void ToggleLineComment(EditorControl& editor, intptr_t line, intptr_t column, bool singleLine, const char* prefix,
                       intptr_t prefixLength, const std::string& padded, bool spaceAfterComment,
                       CommentAction& action) {
    const auto indent = editor.Send(Message::GetLineIndentPosition, line);
    const auto end = editor.Send(Message::GetLineEndPosition, line);
    const auto length = std::min(prefixLength + 1, end - indent);
    const auto* bytes = reinterpret_cast<const char*>(editor.Send(Message::GetRangePointer, indent, length));
    const bool commented = length >= prefixLength && std::memcmp(bytes, prefix, prefixLength) == 0;
    if (action == CommentAction::None) action = commented ? CommentAction::Delete : CommentAction::Add;
    if (!commented) {
        if (action == CommentAction::Add) AddLineComment(editor, line, indent, end, column, singleLine, prefix, padded);
        return;
    }
    if (action == CommentAction::Delete) {
        auto commentStart = indent;
        auto commentEnd = indent + prefixLength;
        if (spaceAfterComment && length > prefixLength && bytes[prefixLength] == ' ') ++commentEnd;
        if (commentEnd == end) commentStart = editor.Send(Message::PositionFromLine, line);
        editor.Send(Message::DeleteRange, commentStart, commentEnd - commentStart);
        return;
    }
    const auto position = editor.Send(Message::FindColumn, line, column);
    if (position != indent) editor.Send(Message::InsertText, position, reinterpret_cast<intptr_t>(padded.c_str()));
}
} // namespace

bool EditorControl::ToggleLineComments(const char* prefix, bool spaceAfterComment) {
    using Scintilla::Message;
    if (Send(Message::SelectionIsRectangle)) return false;

    // Adapted from Notepad4 89b8b36 Edit.cpp::EditToggleLineComments.
    // A trailing space in the original prefix requests a mandatory pad (m4).
    std::string marker(prefix);
    if (marker.ends_with(' ')) {
        marker.pop_back();
        spaceAfterComment = true;
    }
    prefix = marker.c_str();
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

    auto action = CommentAction::None;
    const std::string padded = std::string(prefix) + (spaceAfterComment ? " " : "");
    Send(Message::BeginUndoAction);
    for (auto line = firstLine; line <= lastLine; ++line)
        ToggleLineComment(*this, line, commentColumn, firstLine == lastLine, prefix, prefixLength, padded,
                          spaceAfterComment, action);
    Send(Message::EndUndoAction);
    if (selectionStart != selectionEnd) {
        const auto start = Send(Message::PositionFromLine, firstLine);
        const auto end = Send(Message::PositionFromLine, lastLine + 1);
        Send(Message::SetSel, current == selectionStart ? end : start, current == selectionStart ? start : end);
    }
    return true;
}
