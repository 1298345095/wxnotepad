#include "editor/editor_control.h"
#include "editor/editor_core.h"
#include <algorithm>

using Scintilla::Message;

void EditorControl::SetVisualBraceMatching(bool enabled) {
    matchBraces_ = enabled;
    if (enabled)
        core_->UpdateVisualBraceMatching();
    else
        // Original toggle clears the decoration while retaining the guide.
        Send(Message::BraceHighlight, static_cast<uintptr_t>(-1), -1);
}

void EditorControl::Core::UpdateVisualBraceMatching() {
    if (!window_.matchBraces_) return;
    // Original Notepad4.cpp::SCN_UPDATEUI at 89b8b36. Keep style/nesting
    // decisions in the pinned core, including nonempty selections and UTF-8.
    const auto isBrace = [](intptr_t ch) {
        return ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == '<' || ch == '>';
    };
    auto position = WndProc(Message::GetCurrentPos, 0, 0);
    if (!isBrace(WndProc(Message::GetCharAt, position, 0))) position = WndProc(Message::PositionBefore, position, 0);
    if (!isBrace(WndProc(Message::GetCharAt, position, 0))) {
        WndProc(Message::BraceHighlight, static_cast<uintptr_t>(-1), -1);
        WndProc(Message::SetHighlightGuide, 0, 0);
        return;
    }
    const auto match = WndProc(Message::BraceMatch, position, -1);
    if (match >= 0) {
        WndProc(Message::BraceHighlight, position, match);
        const auto column = std::min(WndProc(Message::GetColumn, position, -1), WndProc(Message::GetColumn, match, -1));
        WndProc(Message::SetHighlightGuide, column, 0);
    } else {
        WndProc(Message::BraceBadLight, position, 0);
        WndProc(Message::SetHighlightGuide, 0, 0);
    }
}

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
