#include "editor/editor_control.h"
#include <algorithm>
#include <string>

void EditorControl::PadWithSpaces(bool skipEmpty, bool noUndoGroup) {
    // EditPadWithSpaces, Notepad4 89b8b36; see licenses/Notepad4.txt.
    using Scintilla::Message;
    intptr_t maxColumn = 0;
    bool reducedSelection = false;
    intptr_t selectionStart = 0;
    intptr_t selectionEnd = 0;
    intptr_t firstLine = 0;
    intptr_t lastLine = 0;
    intptr_t caretLine = 0;
    intptr_t anchorLine = 0;
    intptr_t caretColumn = 0;
    intptr_t anchorColumn = 0;
    const bool rectangular = Send(Message::SelectionIsRectangle);
    if (!rectangular) {
        selectionStart = Send(Message::GetSelectionStart);
        selectionEnd = Send(Message::GetSelectionEnd);
        firstLine = Send(Message::LineFromPosition, selectionStart);
        lastLine = Send(Message::LineFromPosition, selectionEnd);
        if (firstLine == lastLine) {
            firstLine = 0;
            lastLine = Send(Message::GetLineCount) - 1;
        } else if (selectionEnd <= Send(Message::PositionFromLine, lastLine) && lastLine - firstLine >= 1) {
            --lastLine;
            reducedSelection = true;
        }
        for (auto line = firstLine; line <= lastLine; ++line) {
            const auto end = Send(Message::GetLineEndPosition, line);
            maxColumn = std::max(maxColumn, Send(Message::GetColumn, end, line));
        }
    } else {
        const auto caret = Send(Message::GetCurrentPos);
        const auto anchor = Send(Message::GetAnchor);
        caretLine = Send(Message::LineFromPosition, caret);
        anchorLine = Send(Message::LineFromPosition, anchor);
        caretColumn = Send(Message::GetColumn, caret, caretLine);
        anchorColumn = Send(Message::GetColumn, anchor, anchorLine);
        lastLine = Send(Message::GetLineCount) - 1;
        for (auto line = firstLine; line <= lastLine; ++line) {
            const auto end = Send(Message::GetLineSelEndPosition, line);
            if (end >= 0) maxColumn = std::max(maxColumn, Send(Message::GetColumn, end, line));
        }
    }

    const std::string padding(static_cast<size_t>(maxColumn), ' ');
    if (!noUndoGroup) Send(Message::BeginUndoAction);
    for (auto line = firstLine; line <= lastLine; ++line) {
        const auto selectedEnd = Send(Message::GetLineSelEndPosition, line);
        if (rectangular && selectedEnd < 0) continue;
        const auto end = Send(Message::GetLineEndPosition, line);
        if (rectangular && end > selectedEnd) continue;
        if (skipEmpty && Send(Message::PositionFromLine, line) >= end) continue;
        const auto length = maxColumn - Send(Message::GetColumn, end, line);
        Send(Message::SetTargetStart, end);
        Send(Message::SetTargetEnd, end);
        Send(Message::ReplaceTarget, length, reinterpret_cast<intptr_t>(padding.data()));
    }
    if (!noUndoGroup) Send(Message::EndUndoAction);

    if (!rectangular &&
        Send(Message::LineFromPosition, selectionStart) != Send(Message::LineFromPosition, selectionEnd)) {
        const auto caret = Send(Message::GetCurrentPos);
        const auto anchor = Send(Message::GetAnchor);
        const auto start = Send(Message::PositionFromLine, firstLine);
        const auto end = reducedSelection ? Send(Message::PositionFromLine, lastLine + 1)
                                          : Send(Message::GetLineEndPosition, lastLine);
        Send(Message::SetSel, caret < anchor ? end : start, caret < anchor ? start : end);
    } else if (rectangular) {
        const auto caret = Send(Message::FindColumn, caretLine, caretColumn);
        const auto anchor = Send(Message::FindColumn, anchorLine, anchorColumn);
        Send(Message::SetRectangularSelectionCaret, caret);
        Send(Message::SetRectangularSelectionAnchor, anchor);
    }
}
