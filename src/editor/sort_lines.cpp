#include "editor/editor_control.h"
#include "editor/platform/custom_case.h"
#include "editor/platform/sort_collation.h"
#include "UniConversion.h"
#include <ScintillaTypes.h>
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.

void EditorControl::MergeDuplicateLines() {
    SortLines(SortOption::DontSort | SortOption::MergeDuplicate);
}
void EditorControl::RemoveDuplicateLines() {
    SortLines(SortOption::DontSort | SortOption::RemoveDuplicate);
}

void EditorControl::SortLines(unsigned options) {
    // EditSortLines, CmpSortLine and DontSortLine from Notepad4 89b8b36.
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
    if (rectangular) PadWithSpaces(!(options & SortOption::Shuffle), true);
    auto targetStart = Send(Message::PositionFromLine, firstLine);
    auto targetEnd = Send(Message::PositionFromLine, lastLine + 1);
    struct Line {
        std::wstring text;
        intptr_t index;
        bool dropped = false;
        std::wstring sortText;
        size_t entryOffset = 0;
        std::optional<OriginalLogicalKey> entryKey;
        std::optional<OriginalLogicalKey> lineKey;
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
        Line entry{std::move(text), line};
        entry.sortText = options & SortOption::IgnoreCase ? OriginalNlsUppercase(entry.text) : entry.text;
        if (options & SortOption::Column) {
            const auto column = rectangular ? std::min(caretColumn, anchorColumn) : caretColumn;
            const auto tabWidth = Send(Message::GetTabWidth);
            intptr_t current = 0;
            intptr_t tabs = tabWidth;
            while (entry.entryOffset < entry.sortText.size()) {
                const auto ch = entry.sortText[entry.entryOffset];
                if (ch == L'\t') {
                    if (current + tabs > column) break;
                    current += tabs;
                    tabs = tabWidth;
                    ++entry.entryOffset;
                } else if (current < column) {
                    ++current;
                    if (--tabs == 0) tabs = tabWidth;
                    ++entry.entryOffset;
                    if (ch >= 0xd800 && ch <= 0xdbff && entry.entryOffset < entry.sortText.size() &&
                        entry.sortText[entry.entryOffset] >= 0xdc00 && entry.sortText[entry.entryOffset] <= 0xdfff)
                        ++entry.entryOffset;
                } else {
                    break;
                }
            }
        } else if (options & SortOption::FileType) {
            entry.entryOffset = OriginalExtensionOffset(entry.sortText);
        }
        if (options & SortOption::LogicalNumber) {
            entry.entryKey.emplace(std::wstring_view(entry.sortText).substr(entry.entryOffset));
            entry.lineKey.emplace(entry.sortText);
        }
        lines.push_back(std::move(entry));
    }
    if (options & SortOption::Shuffle) {
        auto state = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
        for (size_t i = lines.size() - 1; i > 0; --i) {
            state = state * 1103515245 + 12345;
            const size_t index = ((state >> 16) & 0x7fff) % i;
            std::swap(lines[i], lines[index]);
        }
    } else {
        std::sort(lines.begin(), lines.end(), [options](const Line& left, const Line& right) {
            const bool secondary = options & (SortOption::Column | SortOption::FileType);
            int compared = 0;
            if (options & SortOption::LogicalNumber) {
                compared = left.entryKey->Compare(*right.entryKey);
                if (compared == 0 && secondary) compared = left.lineKey->Compare(*right.lineKey);
            }
            if (compared == 0) {
                compared = std::wstring_view(left.sortText)
                               .substr(left.entryOffset)
                               .compare(std::wstring_view(right.sortText).substr(right.entryOffset));
                if (compared == 0 && secondary) compared = left.sortText.compare(right.sortText);
                if (compared == 0) {
                    compared = left.index < right.index ? -1 : left.index > right.index ? 1 : 0;
                    if (options & SortOption::MergeDuplicate) compared = -compared;
                }
            }
            if (options & SortOption::Descending) compared = -compared;
            return compared < 0;
        });
        if (options > SortOption::Shuffle) {
            bool lastDuplicate = false;
            for (size_t i = 0; i < lines.size(); ++i) {
                unsigned drop;
                if (i + 1 < lines.size() && lines[i].sortText == lines[i + 1].sortText) {
                    lastDuplicate = true;
                    drop = SortOption::MergeDuplicate | SortOption::RemoveDuplicate;
                } else {
                    drop = lastDuplicate ? SortOption::RemoveDuplicate : SortOption::RemoveUnique;
                    lastDuplicate = false;
                }
                lines[i].dropped = (options & drop) != 0;
            }
        }
        if (options & SortOption::DontSort)
            std::sort(lines.begin(), lines.end(),
                      [](const Line& left, const Line& right) { return left.index < right.index; });
    }
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
