#include "editor/editor_control.h"
#include <utility>
#include <ScintillaTypes.h>
#include <ScintillaStructures.h>

namespace {
using Scintilla::Message;
constexpr auto FoldHeader = static_cast<intptr_t>(std::to_underlying(Scintilla::FoldLevel::HeaderFlag));
constexpr auto FoldNumber = static_cast<intptr_t>(std::to_underlying(Scintilla::FoldLevel::NumberMask));

intptr_t PreviousBlock(EditorControl& editor, intptr_t current, intptr_t line, intptr_t level, bool sibling) {
    auto candidate = current - 1;
    intptr_t first = -1;
    level &= FoldNumber;
    while (candidate >= 0) {
        const auto candidateLevel = editor.Send(Message::GetFoldLevel, candidate);
        if (!(candidateLevel & FoldHeader) || candidate == line) {
            --candidate;
            continue;
        }
        if (!sibling || level >= (candidateLevel & FoldNumber)) return candidate;
        if (first < 0) first = candidate;
        candidate = editor.Send(Message::GetFoldParent, candidate);
    }
    return sibling && first >= 0 ? first : line;
}

intptr_t NextBlock(EditorControl& editor, intptr_t current, intptr_t line, intptr_t level, bool sibling) {
    using enum Scintilla::Message;
    const auto count = editor.Send(GetLineCount);
    if (line >= 0) line = editor.Send(GetLastChild, line, -1);
    auto candidate = current + 1;
    intptr_t first = -1;
    if (sibling && line > 0 && (level & FoldHeader)) candidate = line + 1;
    level &= FoldNumber;
    while (candidate < count) {
        const auto candidateLevel = editor.Send(GetFoldLevel, candidate);
        if (!(candidateLevel & FoldHeader)) {
            ++candidate;
            continue;
        }
        if (!sibling || level >= (candidateLevel & FoldNumber)) return candidate;
        if (first < 0) first = candidate;
        candidate = editor.Send(GetLastChild, candidate, candidateLevel) + 1;
    }
    return sibling && first >= 0 ? first : line;
}
} // namespace

void EditorControl::FocusSelectionBoundary(bool start) {
    using Scintilla::Message;
    const auto anchor = Send(Message::GetAnchor);
    const auto caret = Send(Message::GetCurrentPos);
    if (!Send(Message::SelectionIsRectangle) && ((start && caret > anchor) || (!start && caret < anchor))) {
        const auto mode = Send(Message::GetSelectionMode);
        Send(Message::SetSel, caret, anchor);
        Send(Message::SetSelectionMode, mode);
        Send(Message::ChooseCaretX);
    }
}

// Adapted from Notepad4 Edit.cpp::EditGotoBlock at 89b8b36; see licenses/Notepad4.txt.
void EditorControl::GoToBlock(BlockNavigation command) {
    using Scintilla::Message;
    const auto position = Send(Message::GetCurrentPos);
    const auto current = Send(Message::LineFromPosition, position);
    auto line = current;
    const auto level = Send(Message::GetFoldLevel, line);
    if (!(level & FoldHeader)) line = Send(Message::GetFoldParent, line);
    switch (command) {
        using enum BlockNavigation;
    case Start:
        break;
    case End:
        if (line >= 0) line = Send(Message::GetLastChild, line, -1);
        break;
    case Previous:
    case PreviousSibling: {
        line = PreviousBlock(*this, current, line, level, command == PreviousSibling);
        break;
    }
    case Next:
    case NextSibling: {
        line = NextBlock(*this, current, line, level, command == NextSibling);
        break;
    }
    }
    if (line >= 0 && line != current) {
        const Scintilla::CharacterRangeFull range{line, Send(Message::GetColumn, position, current)};
        JumpToPosition(Send(Message::FindColumnEx,
                            static_cast<uintptr_t>(std::to_underlying(Scintilla::ColumnType::Default)),
                            reinterpret_cast<intptr_t>(&range)));
    }
}
