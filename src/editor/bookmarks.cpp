#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <cstdlib>

using Scintilla::Message;

void EditorControl::ToggleBookmark() {
    // Original EditToggleBookmarkAt at 89b8b36; marker zero is shared with Find All.
    const auto line = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
    Send((Send(Message::MarkerGet, line) & 1) ? Message::MarkerDelete : Message::MarkerAdd, line, 0);
}

void EditorControl::GoToBookmark(bool forwards) {
    // Original Notepad4.cpp::BME_EDIT_BOOKMARKNEXT/PREV, including its caret policy.
    const auto line = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
    const auto message = forwards ? Message::MarkerNext : Message::MarkerPrevious;
    auto target = Send(message, forwards ? line + 1 : line - 1, 1);
    if (target < 0) target = Send(message, forwards ? 0 : Send(Message::GetLineCount), 1);
    if (target < 0) return;
    Occurrences().IgnoreNextSelectionUpdate();
    Send(Message::EnsureVisible, target);
    Send(Message::GotoLine, target);
    Send(Message::SetYCaretPolicy,
         static_cast<uintptr_t>(Scintilla::CaretPolicy::Slop) | static_cast<uintptr_t>(Scintilla::CaretPolicy::Strict) |
             static_cast<uintptr_t>(Scintilla::CaretPolicy::Even),
         10);
    Send(Message::ScrollCaret);
    Send(Message::SetYCaretPolicy, static_cast<uintptr_t>(Scintilla::CaretPolicy::Even), 0);
}

void EditorControl::SelectAllBookmarks() {
    // Original EditBookmarkSelectAll: reverse whole-line ranges, earliest nearest tie.
    auto line = Send(Message::MarkerNext, 0, 1);
    if (line < 0) return;
    const auto current = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
    Send(Message::SetSelection, Send(Message::PositionFromLine, line), Send(Message::PositionFromLine, line + 1));
    uintptr_t main = 0;
    uintptr_t selection = 0;
    auto minimum = std::abs(line - current);
    while ((line = Send(Message::MarkerNext, line + 1, 1)) >= 0) {
        Send(Message::AddSelection, Send(Message::PositionFromLine, line), Send(Message::PositionFromLine, line + 1));
        ++selection;
        const auto difference = std::abs(line - current);
        if (difference < minimum) {
            minimum = difference;
            main = selection;
        }
    }
    Send(Message::SetMainSelection, main);
}
