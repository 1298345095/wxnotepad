#include "editor/line_navigation.h"
#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <ScintillaStructures.h>
#include <cwchar>

// Adapted from Notepad4 EditLineNumDlgProc, EditJumpTo and EditSelectEx at
// 89b8b36e0ee2c23f73be922315f17261ccfbbe1a; see licenses/Notepad4.txt.
GoToResult GoToLocation(EditorControl& editor, const wxString& line, const wxString& column) {
    using Scintilla::Message;
    const auto parse = [](const wxString& input, intptr_t& value) {
        const auto& text = input.ToStdWstring();
        wchar_t* end = nullptr;
        // CRTStrToInt64 accepts a decimal prefix and saturates on overflow.
        value = static_cast<intptr_t>(std::wcstoll(text.c_str(), &end, 10));
        return end != text.c_str();
    };
    intptr_t newLine = 0;
    intptr_t newColumn = 0;
    const bool translatedLine = parse(line, newLine);
    bool translatedColumn;
    if (column.empty()) {
        newColumn = 1;
        translatedColumn = translatedLine;
    } else {
        translatedColumn = parse(column, newColumn);
    }
    if (!(translatedLine || translatedColumn)) return GoToResult::InvalidLine;

    intptr_t position;
    if (translatedColumn && !translatedLine) {
        if (newColumn <= 0 || newColumn > editor.Send(Message::GetLength)) return GoToResult::InvalidColumn;
        position = editor.Send(Message::PositionBefore, newColumn);
    } else {
        if (newLine <= 0 || newLine > editor.Send(Message::GetLineCount))
            return newColumn > 0 ? GoToResult::InvalidLine : GoToResult::InvalidColumn;
        auto type = Scintilla::ColumnType::Default;
        if (newColumn < 0) {
            type = Scintilla::ColumnType::Character;
            newColumn = -(newColumn + 1);
        } else {
            --newColumn;
        }
        const Scintilla::CharacterRangeFull range{newLine - 1, newColumn};
        position = editor.Send(Message::FindColumnEx, static_cast<uintptr_t>(type), reinterpret_cast<intptr_t>(&range));
    }

    // EditSelectEx reveals folded ancestors before moving and centers a target
    // outside the viewport, then restores the original normal caret policies.
    using Scintilla::CaretPolicy;
    const auto even = static_cast<uintptr_t>(CaretPolicy::Even);
    const auto slop = static_cast<uintptr_t>(CaretPolicy::Slop);
    const auto strictPolicy = slop | static_cast<uintptr_t>(CaretPolicy::Strict) | even;
    editor.Send(Message::SetXCaretPolicy, strictPolicy, 50);
    editor.Send(Message::SetYCaretPolicy, strictPolicy, 5);
    editor.Send(Message::EnsureVisibleEnforcePolicy, editor.Send(Message::LineFromPosition, position));
    editor.Send(Message::GotoPos, position);
    editor.Send(Message::SetXCaretPolicy, slop | even, 50);
    editor.Send(Message::SetYCaretPolicy, even, 0);
    editor.Send(Message::ChooseCaretX);
    return GoToResult::Moved;
}

bool GoToLogicalLine(EditorControl& editor, const wxString& input) {
    return GoToLocation(editor, input, {}) == GoToResult::Moved;
}
