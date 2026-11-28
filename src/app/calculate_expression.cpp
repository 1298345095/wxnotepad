#include "app/editor_frame.h"
#include "app/expression_evaluator.h"
#include "editor/editor_control.h"
#include "editor/platform/native_encoding.h"
#include <wx/msgdlg.h>
#include <wx/translation.h>
#include <algorithm>
#include <utility>
#include <vector>

// Adapted from Notepad4 89b8b36 Bridge.cpp::EditCalculateExpr.
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
intptr_t ErrorPosition(EditorControl& editor, [[maybe_unused]] intptr_t start, const ExpressionEvaluation& result) {
    using Scintilla::Message;
#ifdef __WXMSW__
    auto position = start;
    if (result.line)
        position = editor.Send(Message::PositionFromLine, editor.Send(Message::LineFromPosition, start) + result.line);
    // Retain the original system engine's UTF-16-column/byte-position handling.
    return position + static_cast<intptr_t>(result.column);
#else
    // GetSelText concatenates sorted selections, regardless of the main range.
    // Map the engine's source-byte offset through that same public boundary.
    std::vector<std::pair<intptr_t, intptr_t>> ranges;
    for (intptr_t index = 0; index < editor.Send(Message::GetSelections); ++index) {
        const auto selection = static_cast<uintptr_t>(index);
        ranges.emplace_back(editor.Send(Message::GetSelectionNStart, selection),
                            editor.Send(Message::GetSelectionNEnd, selection));
    }
    std::ranges::sort(ranges);
    auto separatorBytes = static_cast<size_t>(editor.Send(Message::GetCopySeparator));
    if (!separatorBytes) {
        const auto eol = static_cast<Scintilla::EndOfLine>(editor.Send(Message::GetEOLMode));
        separatorBytes = eol == Scintilla::EndOfLine::CrLf ? 2 : 1;
    }
    auto offset = result.sourceOffset;
    auto position = ranges.front().first;
    auto target = position;
    for (size_t index = 0; index < ranges.size(); ++index) {
        const auto [first, last] = ranges[index];
        position = first;
        const auto length = static_cast<size_t>(last - first);
        if (offset <= length || index + 1 == ranges.size()) {
            target = first + static_cast<intptr_t>(std::min(offset, length));
            break;
        }
        offset -= length;
        if (offset < separatorBytes) {
            target = last;
            break;
        }
        offset -= separatorBytes;
    }
    // Only real selected UTF-8 character boundaries can anchor the popup.
    while (position < target) {
        const auto next = editor.Send(Message::PositionAfter, position);
        if (next > target) break;
        position = next;
    }
    return position;
#endif
}
} // namespace

void EditorFrame::CalculateExpression() {
    using Scintilla::Message;
    if (editor_->Send(Message::GetSelectionEmpty)) return;
    if (editor_->Send(Message::SelectionIsRectangle)) {
        wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                     wxOK | wxICON_WARNING, this);
        return;
    }
    const auto length = editor_->Send(Message::GetSelText);
    std::string selected(static_cast<size_t>(length) + 1, '\0');
    editor_->Send(Message::GetSelText, 0, reinterpret_cast<intptr_t>(selected.data()));
    auto originalSourceBytes = selected.size();
    if (document_.IsDefaultEncoding()) {
        const auto native = editor_->NativeSelectedBytes(false);
        originalSourceBytes = native.size() + 1;
        selected = NativeDefaultEncodingUnicode(native);
    }
    constexpr size_t OriginalMinimumCharacters = 1024;
    constexpr size_t OriginalAllocationAlignment = 16;
    const auto allocated = std::max(OriginalMinimumCharacters, (originalSourceBytes + OriginalAllocationAlignment - 1) &
                                                                   ~(OriginalAllocationAlignment - 1));
    auto result = EvaluateExpression(wxString::FromUTF8(selected.c_str()), allocated * 2 + OriginalMinimumCharacters);
    if (document_.IsDefaultEncoding()) {
        result.text = NativeDefaultEncodingText(result.text);
        result.error = NativeDefaultEncodingText(result.error);
    }
    const auto start = editor_->Send(Message::GetSelectionStart);
    const auto end = editor_->Send(Message::GetSelectionEnd);
    if (!result.error.empty()) {
        const auto position = ErrorPosition(*editor_, start, result);
        const auto error = result.error.utf8_string();
        editor_->Send(Message::ShowNotification, static_cast<uintptr_t>(position) << 2,
                      reinterpret_cast<intptr_t>(error.c_str()));
    } else if (!result.text.empty()) {
        const auto inserted = (" " + result.text).utf8_string();
        editor_->Send(Message::InsertText, end, reinterpret_cast<intptr_t>(inserted.c_str()));
        editor_->Send(Message::SetSel, end, end + static_cast<intptr_t>(inserted.size()));
    }
}
