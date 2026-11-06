#include "editor/editor_control.h"
#include <string>
#include <string_view>
#include "UniConversion.h"

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
using Scintilla::Message;
using namespace Scintilla::Internal;
std::wstring SelectedUnits(EditorControl& editor, intptr_t start, intptr_t end) {
    const std::string_view bytes(
        reinterpret_cast<const char*>(editor.Send(Message::GetRangePointer, start, end - start)),
        static_cast<size_t>(end - start));
    std::wstring units(UTF16Length(bytes), L'\0');
    UTF16FromUTF8(bytes, units.data(), units.size());
    return units;
}
void ReplaceUnits(EditorControl& editor, intptr_t start, intptr_t end, std::wstring_view units) {
    std::string bytes(UTF8Length(units), '\0');
    UTF8FromUTF16(units, bytes.data(), bytes.size());
    const bool backwards = editor.Send(Message::GetAnchor) > editor.Send(Message::GetCurrentPos);
    editor.Send(Message::SetTargetStart, start);
    editor.Send(Message::SetTargetEnd, end);
    editor.Send(Message::ReplaceTarget, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    const auto resultEnd = start + static_cast<intptr_t>(bytes.size());
    editor.Send(Message::SetSel, backwards ? resultEnd : start, backwards ? start : resultEnd);
}
} // namespace

bool EditorControl::TabsToSpaces(bool onlyIndent) {
    if (Send(Message::GetSelectionEmpty)) return true;
    if (Send(Message::SelectionIsRectangle)) return false;
    // Adapted from Notepad4 89b8b36 Edit.cpp::EditTabsToSpaces. These
    // counters intentionally use UTF-16 units, including on 32-bit-wchar_t hosts.
    const auto start =
        Send(Message::PositionFromLine, Send(Message::LineFromPosition, Send(Message::GetSelectionStart)));
    const auto end = Send(Message::GetSelectionEnd);
    const auto input = SelectedUnits(*this, start, end);
    const auto tabWidth = Send(Message::GetTabWidth);
    std::wstring output;
    bool lineStart = true;
    bool modified = false;
    intptr_t column = 0;
    for (const wchar_t unit : input) {
        if (unit == L'\t' && (!onlyIndent || lineStart)) {
            output.append(static_cast<size_t>(tabWidth - column % tabWidth), L' ');
            column = 0;
            modified = true;
        } else {
            ++column;
            if (unit == L'\n' || unit == L'\r') {
                column = 0;
                lineStart = true;
            } else if (unit != L' ') {
                lineStart = false;
            }
            output += unit;
        }
    }
    if (modified) ReplaceUnits(*this, start, end, output);
    return true;
}

bool EditorControl::SpacesToTabs(bool onlyIndent) {
    if (Send(Message::GetSelectionEmpty)) return true;
    if (Send(Message::SelectionIsRectangle)) return false;
    // Adapted from Notepad4 89b8b36 Edit.cpp::EditSpacesToTabs.
    const auto start =
        Send(Message::PositionFromLine, Send(Message::LineFromPosition, Send(Message::GetSelectionStart)));
    const auto end = Send(Message::GetSelectionEnd);
    const auto input = SelectedUnits(*this, start, end);
    const auto tabWidth = Send(Message::GetTabWidth);
    std::wstring output;
    output.reserve(input.size());
    std::wstring spaces;
    bool lineStart = true;
    bool modified = false;
    intptr_t column = 0;
    for (size_t index = 0; index < input.size(); ++index) {
        const wchar_t unit = input[index];
        if ((unit == L' ' || unit == L'\t') && (!onlyIndent || lineStart)) {
            spaces += unit;
            if (spaces.size() == static_cast<size_t>(tabWidth - column % tabWidth) || unit == L'\t') {
                const auto next = index + 1 < input.size() ? input[index + 1] : L'\0';
                output += spaces.size() > 1 || next == L' ' || next == L'\t' ? L'\t' : unit;
                column = 0;
                spaces.clear();
                modified = modified || unit != output.back();
            }
        } else {
            column += static_cast<intptr_t>(spaces.size()) + 1;
            output += spaces;
            spaces.clear();
            if (unit == L'\n' || unit == L'\r') {
                column = 0;
                lineStart = true;
            } else {
                lineStart = false;
            }
            output += unit;
        }
    }
    output += spaces;
    if (modified || output.size() != input.size()) ReplaceUnits(*this, start, end, output);
    return true;
}
