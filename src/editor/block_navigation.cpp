#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <ScintillaStructures.h>

// Adapted from Notepad4 Edit.cpp::EditGotoBlock at 89b8b36; see licenses/Notepad4.txt.
void EditorControl::GoToBlock(BlockNavigation command) {
    using Scintilla::Message;
    const auto position = Send(Message::GetCurrentPos);
    const auto current = Send(Message::LineFromPosition, position);
    auto line = current;
    auto level = Send(Message::GetFoldLevel, line);
    constexpr intptr_t header = 0x2000;
    constexpr intptr_t number = 0xFFF;
    if (!(level & header)) line = Send(Message::GetFoldParent, line);
    switch (command) {
    case BlockNavigation::Start:
        break;
    case BlockNavigation::End:
        if (line >= 0) line = Send(Message::GetLastChild, line, -1);
        break;
    case BlockNavigation::Previous:
    case BlockNavigation::PreviousSibling: {
        bool sibling = command == BlockNavigation::PreviousSibling;
        auto candidate = current - 1;
        intptr_t first = -1;
        level &= number;
        while (candidate >= 0) {
            const auto candidateLevel = Send(Message::GetFoldLevel, candidate);
            if ((candidateLevel & header) && candidate != line) {
                if (sibling) {
                    if (first < 0) first = candidate;
                    if (level >= (candidateLevel & number)) {
                        line = candidate;
                        sibling = false;
                        break;
                    }
                    candidate = Send(Message::GetFoldParent, candidate);
                    continue;
                }
                line = candidate;
                break;
            }
            --candidate;
        }
        if (sibling && first >= 0) line = first;
        break;
    }
    case BlockNavigation::Next:
    case BlockNavigation::NextSibling: {
        const auto count = Send(Message::GetLineCount);
        if (line >= 0) line = Send(Message::GetLastChild, line, -1);
        bool sibling = command == BlockNavigation::NextSibling;
        auto candidate = current + 1;
        intptr_t first = -1;
        if (sibling && line > 0 && (level & header)) candidate = line + 1;
        level &= number;
        while (candidate < count) {
            const auto candidateLevel = Send(Message::GetFoldLevel, candidate);
            if (candidateLevel & header) {
                if (sibling) {
                    if (first < 0) first = candidate;
                    if (level >= (candidateLevel & number)) {
                        line = candidate;
                        sibling = false;
                        break;
                    }
                    candidate = Send(Message::GetLastChild, candidate, candidateLevel);
                } else {
                    line = candidate;
                    break;
                }
            }
            ++candidate;
        }
        if (sibling && first >= 0) line = first;
        break;
    }
    }
    if (line >= 0 && line != current) {
        const Scintilla::CharacterRangeFull range{line, Send(Message::GetColumn, position, current)};
        JumpToPosition(Send(Message::FindColumnEx, static_cast<uintptr_t>(Scintilla::ColumnType::Default),
                            reinterpret_cast<intptr_t>(&range)));
    }
}

// Original Edit.cpp::EditSelectLines(currentBlock=true).
void EditorControl::SelectCurrentBlockLines() {
    using Scintilla::Message;
    const auto caret = Send(Message::GetCurrentPos);
    const auto anchor = Send(Message::GetAnchor);
    const auto current = Send(Message::LineFromPosition, caret);
    auto start = current;
    if (!(Send(Message::GetFoldLevel, start) & 0x2000)) {
        start = Send(Message::GetFoldParent, start);
        if (start < 0) {
            Send(Message::SelectAll);
            return;
        }
    }
    const auto end = Send(Message::GetLastChild, start, -1);
    const bool backward = caret < anchor || (caret == anchor && end - current > current - start);
    const bool lineSelection = (static_cast<unsigned>(GetLineSelectionMode()) & 1U) != 0;
    const auto offset = lineSelection ? 0 : 1;
    const auto anchorLine = backward ? end + offset : start;
    const auto caretLine = backward ? start : end + offset;
    Send(Message::SetSel, Send(Message::PositionFromLine, anchorLine), Send(Message::PositionFromLine, caretLine));
    if (lineSelection) Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Lines));
    Send(Message::ChooseCaretX);
}
