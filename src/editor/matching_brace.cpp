#include "editor/editor_control.h"
#include <algorithm>

using Scintilla::Message;

void EditorControl::MatchBrace(bool select) {
    // Notepad4.cpp::IDM_EDIT_FINDMATCHINGBRACE/IDM_EDIT_SELTOMATCHINGBRACE
    // at 89b8b36; see licenses/Notepad4.txt.
    const auto isBrace = [](intptr_t ch) {
        return ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == '<' || ch == '>';
    };
    auto caret = Send(Message::GetCurrentPos);
    auto position = caret;
    if (!isBrace(Send(Message::GetCharAt, position))) position = Send(Message::PositionBefore, position);
    if (!isBrace(Send(Message::GetCharAt, position))) return;
    // The pinned core accepts a start position here; the original wrapper uses -1.
    const auto match = Send(Message::BraceMatch, position, -1);
    if (match < 0) return;
    if (!select) {
        Send(Message::GotoPos, match);
        return;
    }
    auto anchor = Send(Message::GetAnchor);
    const auto start = std::min(anchor, caret);
    const auto end = std::max(anchor, caret);
    if (match > position) {
        anchor = std::min(position, start);
        caret = std::max(match + 1, end);
    } else {
        anchor = std::max(position + 1, end);
        caret = std::min(match, start);
    }
    Send(Message::SetSel, anchor, caret);
}
