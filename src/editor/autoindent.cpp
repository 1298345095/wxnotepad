#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <string>

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
            auto padding = Send(Message::GetIndent);
            char character = ' ';
            if (Send(Message::GetTabIndents)) {
                if (!Send(Message::GetUseTabs))
                    padding = Send(Message::GetTabWidth);
                else {
                    padding = 1;
                    character = '\t';
                }
            }
            insertion.append(static_cast<size_t>(padding), character);
            caret += padding;
        }
        const auto next = Send(Message::GetCharAt, Send(Message::PositionFromLine, line));
        if (delimiter && (next == '}' || next == ']' || next == ')')) {
            const auto eol = static_cast<Scintilla::EndOfLine>(Send(Message::GetEOLMode));
            insertion += eol == Scintilla::EndOfLine::Lf ? "\n" : eol == Scintilla::EndOfLine::Cr ? "\r" : "\r\n";
            insertion.append(previous, 0, static_cast<size_t>(leading));
        }
    }
    if (!insertion.empty()) {
        Send(Message::AddText, insertion.size(), reinterpret_cast<intptr_t>(insertion.data()));
        if (indent) Send(Message::SetSel, caret, caret);
    }
}
