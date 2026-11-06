#include "editor/editor_control.h"
#include "UniConversion.h"
#include <ScintillaTypes.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.

void EditorControl::MergeDuplicateLines() {
    DuplicateLines(true);
}
void EditorControl::RemoveDuplicateLines() {
    DuplicateLines(false);
}

void EditorControl::DuplicateLines(bool merge) {
    // EditSortLines, CmpSortLine and DontSortLine from Notepad4 89b8b36,
    // restricted to DontSort with MergeDuplicate or RemoveDuplicate.
    using Scintilla::Message;
    using namespace Scintilla::Internal;
    auto caret = Send(Message::GetCurrentPos);
    auto anchor = Send(Message::GetAnchor);
    if (caret == anchor) return;
    const bool rectangular = Send(Message::SelectionIsRectangle);
    const auto caretLine = Send(Message::LineFromPosition, caret);
    const auto anchorLine = Send(Message::LineFromPosition, anchor);
    const auto caretColumn = Send(Message::GetColumn, caret, caretLine);
    const auto anchorColumn = Send(Message::GetColumn, anchor, anchorLine);
    auto firstLine = std::min(caretLine, anchorLine);
    auto lastLine = std::max(caretLine, anchorLine);
    if (!rectangular) {
        const auto end = Send(Message::GetSelectionEnd);
        if (end <= Send(Message::PositionFromLine, lastLine)) --lastLine;
    }
    if (lastLine - firstLine + 1 < 2) return;

    Send(Message::BeginUndoAction);
    if (rectangular) PadWithSpaces(true, true);
    auto targetStart = Send(Message::PositionFromLine, firstLine);
    auto targetEnd = Send(Message::PositionFromLine, lastLine + 1);
    struct Line {
        std::wstring text;
        intptr_t index;
        bool dropped = false;
    };
    std::vector<Line> lines;
    lines.reserve(static_cast<size_t>(lastLine - firstLine + 1));
    for (auto line = firstLine; line <= lastLine; ++line) {
        const auto start = Send(Message::PositionFromLine, line);
        const auto end = Send(Message::GetLineEndPosition, line);
        std::string_view bytes(reinterpret_cast<const char*>(Send(Message::GetRangePointer, start, end - start)),
                               static_cast<size_t>(end - start));
        // The original conversion reads NUL-terminated line text.
        bytes = bytes.substr(0, bytes.find('\0'));
        std::wstring text(UTF16Length(bytes), L'\0');
        UTF16FromUTF8(bytes, text.data(), text.size());
        lines.push_back({std::move(text), line});
    }
    std::sort(lines.begin(), lines.end(), [merge](const Line& left, const Line& right) {
        const int compared = left.text.compare(right.text);
        if (compared != 0) return compared < 0;
        return merge ? left.index > right.index : left.index < right.index;
    });
    bool lastDuplicate = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i + 1 < lines.size() && lines[i].text == lines[i + 1].text) {
            lastDuplicate = true;
            lines[i].dropped = true;
        } else {
            lines[i].dropped = lastDuplicate && !merge;
            lastDuplicate = false;
        }
    }
    std::sort(lines.begin(), lines.end(), [](const Line& left, const Line& right) { return left.index < right.index; });
    const auto mode = static_cast<Scintilla::EndOfLine>(Send(Message::GetEOLMode));
    const std::string_view eol = mode == Scintilla::EndOfLine::CrLf ? "\r\n"
                                 : mode == Scintilla::EndOfLine::Cr ? "\r"
                                                                    : "\n";
    std::string output;
    for (const auto& line : lines) {
        if (line.dropped) continue;
        const auto start = output.size();
        const auto length = UTF8Length(line.text);
        output.resize(start + length);
        UTF8FromUTF16(line.text, output.data() + start, length);
        output += eol;
    }
    if (!output.empty() && Send(Message::GetLineEndPosition, lastLine) == targetEnd)
        output.resize(output.size() - eol.size());
    Send(Message::SetTargetRange, targetStart, targetEnd);
    // Ordinary replacement intentionally preserves the original identical-byte history.
    Send(Message::ReplaceTarget, output.size(), reinterpret_cast<intptr_t>(output.data()));
    Send(Message::EndUndoAction);
    if (!rectangular) {
        const auto end = targetStart + static_cast<intptr_t>(output.size());
        Send(Message::SetSel, anchor > caret ? end : targetStart, anchor > caret ? targetStart : end);
    } else {
        targetStart = Send(Message::GetTargetStart);
        targetEnd = Send(Message::GetTargetEnd);
        Send(Message::ClearSelections);
        if (targetStart != targetEnd) {
            // Preserve the original endpoint subtraction even for an unterminated result.
            targetEnd -= static_cast<intptr_t>(eol.size());
            firstLine = Send(Message::LineFromPosition, targetStart);
            lastLine = Send(Message::LineFromPosition, targetEnd);
            caret = Send(Message::FindColumn, anchorLine > caretLine ? firstLine : lastLine, caretColumn);
            anchor = Send(Message::FindColumn, anchorLine > caretLine ? lastLine : firstLine, anchorColumn);
            if (caret != anchor) {
                Send(Message::SetRectangularSelectionCaret, caret);
                Send(Message::SetRectangularSelectionAnchor, anchor);
            } else {
                Send(Message::SetSel, targetStart, targetStart);
            }
        } else {
            Send(Message::SetSel, targetStart, targetStart);
        }
    }
}
