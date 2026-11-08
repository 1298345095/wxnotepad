#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <string>
#include <string_view>

using Scintilla::Message;

namespace {
std::string IndentPadding(EditorControl& editor) {
    auto padding = editor.Send(Message::GetIndent);
    char character = ' ';
    if (editor.Send(Message::GetTabIndents)) {
        if (!editor.Send(Message::GetUseTabs))
            padding = editor.Send(Message::GetTabWidth);
        else {
            padding = 1;
            character = '\t';
        }
    }
    return std::string(static_cast<size_t>(padding), character);
}

void AppendClosingDelimiterLine(EditorControl& editor, intptr_t line, bool delimiter,
                                std::string_view leadingWhitespace, std::string& insertion) {
    const auto next = editor.Send(Message::GetCharAt, editor.Send(Message::PositionFromLine, line));
    if (delimiter && (next == '}' || next == ']' || next == ')')) {
        const auto eol = static_cast<Scintilla::EndOfLine>(editor.Send(Message::GetEOLMode));
        insertion += eol == Scintilla::EndOfLine::Lf ? "\n" : eol == Scintilla::EndOfLine::Cr ? "\r" : "\r\n";
        insertion += leadingWhitespace;
    }
}
} // namespace

void EditorControl::IndentLines(bool forwards) {
    if (!Send(Message::GetReadOnly) && Send(Message::GetSelectionEmpty) && Send(Message::GetSelections) == 1 &&
        Send(Message::GetSelectionMode) == static_cast<intptr_t>(Scintilla::SelectionMode::Stream)) {
        // The core excludes an endpoint at a line start. Move an empty
        // selection to its line end so the explicit line command includes it.
        const auto line = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
        Send(Message::GotoPos, Send(Message::GetLineEndPosition, line));
    }
    Send(forwards ? Message::LineIndent : Message::LineDedent);
}

// Adapted from Notepad4 EditAutoC.cpp::EditAutoIndent, pinned 89b8b36.
// Copyright and license: licenses/Notepad4.txt. The delivered schemes have no
// EditKeywordIndent cases; delimiter/colon/comment rules are scheme independent.
void EditorControl::AutoIndent() {
    using Scintilla::Message;
    const auto position = Send(Message::GetCurrentPos);
    const auto line = Send(Message::LineFromPosition, position);
    if (line <= 0) return;
    const auto previousStart = Send(Message::PositionFromLine, line - 1);
    const auto previousEnd = Send(Message::GetLineEndPosition, line - 1);
    if (previousEnd == previousStart && (Send(Message::MarkerGet, line - 1) & 1)) {
        Send(Message::MarkerDelete, line - 1, 0);
        Send(Message::MarkerAdd, line, 0);
    }
    const auto length = Send(Message::LineLength, line - 1);
    if (length < 2) return;
    std::string previous(static_cast<size_t>(length) + 1, '\0');
    Send(Message::GetLine, line - 1, reinterpret_cast<intptr_t>(previous.data()));
    const auto contentLength = static_cast<size_t>(previousEnd - previousStart);
    const char last = contentLength ? previous[contentLength - 1] : '\0';
    const bool delimiter = last == '{' || last == '[' || last == '(';
    const bool comment = (last == '*' || last == '!') && contentLength >= 3 && previous[contentLength - 3] == '/' &&
                         previous[contentLength - 2] == '*';
    const bool indent = delimiter || last == ':' || comment;
    const auto first = previous.find_first_not_of(" \t");
    std::string insertion = previous.substr(0, first);
    const auto leading = static_cast<intptr_t>(insertion.size());
    auto caret = position + leading;
    if (indent) {
        if (comment) {
            insertion += " *";
            caret += 2;
        } else {
            const auto padding = IndentPadding(*this);
            insertion += padding;
            caret += static_cast<intptr_t>(padding.size());
        }
        AppendClosingDelimiterLine(*this, line, delimiter,
                                   std::string_view(previous.data(), static_cast<size_t>(leading)), insertion);
    }
    if (!insertion.empty()) {
        Send(Message::AddText, insertion.size(), reinterpret_cast<intptr_t>(insertion.data()));
        if (indent) Send(Message::SetSel, caret, caret);
    }
}
