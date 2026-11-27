#include "editor/editor_control.h"
#include <algorithm>
#include <string>

namespace {
using Scintilla::Message;
struct PaddingSelection {
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
    bool rectangular;

    explicit PaddingSelection(EditorControl& editor) : rectangular(editor.Send(Message::SelectionIsRectangle)) {
        using enum Scintilla::Message;
        if (!rectangular) {
            selectionStart = editor.Send(GetSelectionStart);
            selectionEnd = editor.Send(GetSelectionEnd);
            firstLine = editor.Send(LineFromPosition, selectionStart);
            lastLine = editor.Send(LineFromPosition, selectionEnd);
            if (firstLine == lastLine) {
                firstLine = 0;
                lastLine = editor.Send(GetLineCount) - 1;
            } else if (selectionEnd <= editor.Send(PositionFromLine, lastLine)) {
                --lastLine;
                reducedSelection = true;
            }
            for (auto line = firstLine; line <= lastLine; ++line) {
                const auto end = editor.Send(GetLineEndPosition, line);
                maxColumn = std::max(maxColumn, editor.Send(GetColumn, end, line));
            }
        } else {
            const auto caret = editor.Send(GetCurrentPos);
            const auto anchor = editor.Send(GetAnchor);
            caretLine = editor.Send(LineFromPosition, caret);
            anchorLine = editor.Send(LineFromPosition, anchor);
            caretColumn = editor.Send(GetColumn, caret, caretLine);
            anchorColumn = editor.Send(GetColumn, anchor, anchorLine);
            lastLine = editor.Send(GetLineCount) - 1;
            for (auto line = firstLine; line <= lastLine; ++line) {
                const auto end = editor.Send(GetLineSelEndPosition, line);
                if (end >= 0) maxColumn = std::max(maxColumn, editor.Send(GetColumn, end, line));
            }
        }
    }

    void Restore(EditorControl& editor) const {
        using enum Scintilla::Message;
        if (!rectangular &&
            editor.Send(LineFromPosition, selectionStart) != editor.Send(LineFromPosition, selectionEnd)) {
            const auto caret = editor.Send(GetCurrentPos);
            const auto anchor = editor.Send(GetAnchor);
            const auto start = editor.Send(PositionFromLine, firstLine);
            const auto end = reducedSelection ? editor.Send(PositionFromLine, lastLine + 1)
                                              : editor.Send(GetLineEndPosition, lastLine);
            editor.Send(SetSel, caret < anchor ? end : start, caret < anchor ? start : end);
        } else if (rectangular) {
            const auto caret = editor.Send(FindColumn, caretLine, caretColumn);
            const auto anchor = editor.Send(FindColumn, anchorLine, anchorColumn);
            editor.Send(SetRectangularSelectionCaret, caret);
            editor.Send(SetRectangularSelectionAnchor, anchor);
        }
    }
};
} // namespace

void EditorControl::PadWithSpaces(bool skipEmpty, bool noUndoGroup) {
    // EditPadWithSpaces, Notepad4 89b8b36; see licenses/Notepad4.txt.
    const PaddingSelection selection(*this);

    const std::string padding(static_cast<size_t>(selection.maxColumn), ' ');
    if (!noUndoGroup) Send(Message::BeginUndoAction);
    for (auto line = selection.firstLine; line <= selection.lastLine; ++line) {
        const auto selectedEnd = Send(Message::GetLineSelEndPosition, line);
        if (selection.rectangular && selectedEnd < 0) continue;
        const auto end = Send(Message::GetLineEndPosition, line);
        if (selection.rectangular && end > selectedEnd) continue;
        if (skipEmpty && Send(Message::PositionFromLine, line) >= end) continue;
        const auto length = selection.maxColumn - Send(Message::GetColumn, end, line);
        Send(Message::SetTargetStart, end);
        Send(Message::SetTargetEnd, end);
        Send(Message::ReplaceTarget, length, reinterpret_cast<intptr_t>(padding.data()));
    }
    if (!noUndoGroup) Send(Message::EndUndoAction);

    selection.Restore(*this);
}
