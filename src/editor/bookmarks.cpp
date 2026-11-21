#include "editor/editor_control.h"
#include "editor/editor_ids.h"
#include <utility>
#include <ScintillaTypes.h>
#include <cstdlib>

using Scintilla::Message;

void EditorControl::ToggleBookmark() {
    // Original EditToggleBookmarkAt at 89b8b36; the marker is shared with Find All.
    const auto line = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
    Send((Send(Message::MarkerGet, line) & EditorMarker::BookmarkMask) ? Message::MarkerDelete : Message::MarkerAdd,
         line, EditorMarker::Bookmark);
}

void EditorControl::GoToBookmark(bool forwards) {
    using enum Scintilla::CaretPolicy;
    // Original Notepad4.cpp::BME_EDIT_BOOKMARKNEXT/PREV, including its caret policy.
    const auto line = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
    const auto message = forwards ? Message::MarkerNext : Message::MarkerPrevious;
    auto target = Send(message, forwards ? line + 1 : line - 1, EditorMarker::BookmarkMask);
    if (target < 0) target = Send(message, forwards ? 0 : Send(Message::GetLineCount), EditorMarker::BookmarkMask);
    if (target < 0) return;
    Occurrences().IgnoreNextSelectionUpdate();
    Send(Message::EnsureVisible, target);
    Send(Message::GotoLine, target);
    Send(Message::SetYCaretPolicy,
         static_cast<uintptr_t>(std::to_underlying(Slop)) | static_cast<uintptr_t>(std::to_underlying(Strict)) |
             static_cast<uintptr_t>(std::to_underlying(Even)),
         10);
    Send(Message::ScrollCaret);
    Send(Message::SetYCaretPolicy, static_cast<uintptr_t>(std::to_underlying(Even)), 0);
}

void EditorControl::SelectAllBookmarks() {
    // Original EditBookmarkSelectAll: reverse whole-line ranges, earliest nearest tie.
    auto line = Send(Message::MarkerNext, 0, EditorMarker::BookmarkMask);
    if (line < 0) return;
    const auto current = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
    Send(Message::SetSelection, Send(Message::PositionFromLine, line), Send(Message::PositionFromLine, line + 1));
    uintptr_t main = 0;
    uintptr_t selection = 0;
    auto minimum = std::abs(line - current);
    while ((line = Send(Message::MarkerNext, line + 1, EditorMarker::BookmarkMask)) >= 0) {
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
