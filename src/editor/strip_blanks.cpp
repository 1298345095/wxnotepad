#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <ScintillaStructures.h>
#include <string_view>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.

namespace {
void StripSelectedBlanks(EditorControl& editor, bool leading) {
    using Scintilla::Message;
    // EditReplaceAllInSelection searches through EOF with real line anchors,
    // accepting only complete matches inside the live selection endpoint.
    const std::string_view pattern = leading ? "^[ \t]+" : "[ \t]+$";
    Scintilla::TextToFindFull find{{editor.Send(Message::GetSelectionStart), editor.Send(Message::GetLength)},
                                   pattern.data(),
                                   static_cast<Scintilla::Position>(pattern.size()),
                                   {0, 0}};
    bool editing = false;
    while (editor.Send(Message::FindTextFull, static_cast<uintptr_t>(Scintilla::FindOption::RegExp),
                       reinterpret_cast<intptr_t>(&find)) >= 0) {
        if (find.chrgText.cpMax > editor.Send(Message::GetSelectionEnd)) break;
        if (!editing) {
            editor.Send(Message::BeginUndoAction);
            editing = true;
        }
        editor.Send(Message::SetTargetStart, find.chrgText.cpMin);
        editor.Send(Message::SetTargetEnd, find.chrgText.cpMax);
        const auto inserted = editor.Send(Message::ReplaceTarget, 0, reinterpret_cast<intptr_t>(""));
        find.chrg.cpMin = find.chrgText.cpMin + inserted;
        find.chrg.cpMax += find.chrg.cpMin - find.chrgText.cpMax;
        if (find.chrg.cpMin == find.chrg.cpMax) break;
        if (leading) {
            const auto line = editor.Send(Message::LineFromPosition, find.chrg.cpMin);
            if (editor.Send(Message::PositionFromLine, line) == find.chrg.cpMin)
                find.chrg.cpMin = editor.Send(Message::PositionFromLine, line + 1);
            if (find.chrg.cpMin == find.chrg.cpMax) break;
        }
    }
    if (editing) {
        editor.Send(Message::EndUndoAction);
        const auto end = editor.Send(Message::GetTargetEnd);
        if (editor.Send(Message::GetSelectionEnd) < end) {
            const auto anchor = editor.Send(Message::GetAnchor);
            const auto caret = editor.Send(Message::GetCurrentPos);
            editor.Send(Message::SetSel, anchor > caret ? end : anchor, anchor > caret ? caret : end);
        }
    }
}

void StripBlanks(EditorControl& editor, bool leading) {
    using Scintilla::Message;
    // Notepad4 89b8b36 EditStripLeadingBlanks / EditStripTrailingBlanks.
    if (!editor.Send(Message::GetSelectionEmpty) && !editor.Send(Message::SelectionIsRectangle)) {
        StripSelectedBlanks(editor, leading);
        return;
    }
    editor.Send(Message::BeginUndoAction);
    const auto lines = editor.Send(Message::GetLineCount);
    for (intptr_t line = 0; line < lines; ++line) {
        const auto start = editor.Send(Message::PositionFromLine, line);
        if (leading) {
            const auto end = editor.Send(Message::GetLineIndentPosition, line);
            if (end > start) editor.Send(Message::DeleteRange, start, end - start);
        } else {
            const auto end = editor.Send(Message::GetLineEndPosition, line);
            auto position = end;
            while (position >= start) {
                const auto ch = editor.Send(Message::GetCharAt, --position);
                if (ch != ' ' && ch != '\t') break;
            }
            if (++position < end) editor.Send(Message::DeleteRange, position, end - position);
        }
    }
    editor.Send(Message::EndUndoAction);
}
} // namespace

void EditorControl::StripLeadingBlanks() {
    StripBlanks(*this, true);
}

void EditorControl::StripTrailingBlanks() {
    StripBlanks(*this, false);
}
