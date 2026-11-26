#include "editor/go_to_location.h"
#include <utility>
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
        position = editor.Send(Message::FindColumnEx, static_cast<uintptr_t>(std::to_underlying(type)),
                               reinterpret_cast<intptr_t>(&range));
    }

    editor.JumpToPosition(position);
    return GoToResult::Moved;
}

bool GoToLogicalLine(EditorControl& editor, const wxString& input) {
    return GoToLocation(editor, input, {}) == GoToResult::Moved;
}

void GoToInitialLocation(EditorControl& editor, InitialLocation location) {
    using Scintilla::Message;
    intptr_t position;
    if (location.line < 0 || location.line > editor.Send(Message::GetLineCount)) {
        position = editor.Send(Message::GetLength);
    } else {
        if (location.column < 0) {
            location.columnType = std::to_underlying(Scintilla::ColumnType::Character);
            location.column = -(location.column + 1);
        } else {
            --location.column;
        }
        const Scintilla::CharacterRangeFull range{location.line ? location.line - 1 : 0, location.column};
        position = editor.Send(Message::FindColumnEx, location.columnType, reinterpret_cast<intptr_t>(&range));
    }
    editor.JumpToInitialPosition(position);
}
