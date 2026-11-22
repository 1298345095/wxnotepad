#include "editor/editor_control.h"
#include <utility>

// Original Edit.cpp::EditReplaceRange; callers retain their own no-change rules.
void EditorControl::ReplaceRangeAndSelect(intptr_t start, intptr_t end, std::string_view replacement) {
    using enum Scintilla::Message;
    const bool backwards = Send(GetAnchor) > Send(GetCurrentPos);
    Send(SetTargetRange, start, end);
    Send(ReplaceTarget, replacement.size(), reinterpret_cast<intptr_t>(replacement.data()));
    const auto resultEnd = start + static_cast<intptr_t>(replacement.size());
    Send(SetSel, backwards ? resultEnd : start, backwards ? start : resultEnd);
}

// Adapted from Notepad4 Edit.cpp::EditSelectWord at 89b8b36; see licenses/Notepad4.txt.
void EditorControl::SelectWord() {
    using enum Scintilla::Message;
    const auto position = Send(GetCurrentPos);
    if (Send(GetSelectionEmpty)) {
        auto start = Send(WordStartPosition, position, true);
        auto end = Send(WordEndPosition, position, true);
        if (start == end) {
            start = Send(WordEndPosition, position, false);
            end = Send(WordEndPosition, start, true);
        }
        if (start != end) Send(SetSel, start, end);
        if (!Send(GetSelectionEmpty)) return;
    }
    const auto line = Send(LineFromPosition, position);
    Send(SetSel, Send(GetLineIndentPosition, line), Send(GetLineEndPosition, line));
}

// Adapted from Notepad4 Edit.cpp::EditSelectLines at 89b8b36.
void EditorControl::SelectLines(bool currentBlock) {
    using enum Scintilla::Message;
    const bool lineSelection =
        (std::to_underlying(GetLineSelectionMode()) & std::to_underlying(LineSelectionMode::VisualStudio)) != 0;
    if (lineSelection && !currentBlock) {
        Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Lines)));
        return;
    }
    const auto caret = Send(GetCurrentPos);
    const auto anchor = Send(GetAnchor);
    auto caretLine = Send(LineFromPosition, caret);
    auto anchorLine = Send(LineFromPosition, anchor);
    bool backward = caret < anchor;
    if (currentBlock) {
        constexpr auto FoldHeader = static_cast<intptr_t>(std::to_underlying(Scintilla::FoldLevel::HeaderFlag));
        auto start = caretLine;
        if (!(Send(GetFoldLevel, start) & FoldHeader)) {
            start = Send(GetFoldParent, start);
            if (start < 0) {
                Send(SelectAll);
                return;
            }
        }
        const auto end = Send(GetLastChild, start, -1);
        backward = backward || (caret == anchor && end - caretLine > caretLine - start);
        anchorLine = backward ? end : start;
        caretLine = backward ? start : end;
    }
    const auto offset = lineSelection ? 0 : 1;
    if (backward)
        anchorLine += offset;
    else
        caretLine += offset;
    Send(SetSel, Send(PositionFromLine, anchorLine), Send(PositionFromLine, caretLine));
    if (lineSelection)
        Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Lines)));
    Send(ChooseCaretX);
}

void EditorControl::SelectCurrentBlockLines() {
    SelectLines(true);
}
