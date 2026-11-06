#include "editor/editor_control.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
#include <UniConversion.h>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.

AlignmentResult EditorControl::AlignLines(AlignmentMode mode) {
    // Edit.cpp::EditAlignText, Notepad4 89b8b36. Core columns count Unicode
    // characters; the original Windows word lengths count UTF-16 code units.
    using Scintilla::Message;
    if (Send(Message::SelectionIsRectangle)) return AlignmentResult::RectangularSelection;
    const bool reversed = Send(Message::GetCurrentPos) < Send(Message::GetAnchor);
    const auto first = Send(Message::LineFromPosition, Send(Message::GetSelectionStart));
    const auto end = Send(Message::GetSelectionEnd);
    auto last = Send(Message::LineFromPosition, end);
    if (end <= Send(Message::PositionFromLine, last) && last - first >= 1) --last;
    intptr_t minimumIndent = 1024;
    intptr_t maximumColumn = 0;
    for (auto line = first; line <= last; ++line) {
        auto lineEnd = Send(Message::GetLineEndPosition, line);
        const auto indent = Send(Message::GetLineIndentPosition, line);
        if (indent < lineEnd) {
            while (lineEnd > indent) {
                const auto ch = Send(Message::GetCharAt, lineEnd - 1);
                if (ch != ' ' && ch != '\t') break;
                --lineEnd;
            }
            minimumIndent = std::min(minimumIndent, Send(Message::GetLineIndentation, line));
            maximumColumn = std::max(maximumColumn, Send(Message::GetColumn, lineEnd, line));
        }
    }
    const auto result = maximumColumn < 1024 ? AlignmentResult::Complete : AlignmentResult::LinesTooLong;
    if (result == AlignmentResult::Complete) {
        Send(Message::BeginUndoAction);
        for (auto line = first; line <= last; ++line) {
            const auto start = Send(Message::PositionFromLine, line);
            const auto lineEnd = Send(Message::GetLineEndPosition, line);
            const auto indent = Send(Message::GetLineIndentPosition, line);
            if (indent == lineEnd && lineEnd > 0) {
                Send(Message::DeleteRange, start, lineEnd - start);
                continue;
            }
            const auto length = Send(Message::LineLength, line);
            const auto* bytes = reinterpret_cast<const char*>(Send(Message::GetRangePointer, start, length));
            const std::string text(bytes ? bytes : "", static_cast<size_t>(length));
            // The original fixed UTF-16 conversion fails for an overlong input,
            // including line endings, even when its trimmed column fits.
            if (Scintilla::Internal::UTF16Length(text) > 1024) continue;
            std::string_view trimmed(text.c_str());
            const auto firstWord = trimmed.find_first_not_of("\r\n\t ");
            if (firstWord == std::string_view::npos) continue;
            trimmed = trimmed.substr(firstWord, trimmed.find_last_not_of("\r\n\t ") - firstWord + 1);
            std::vector<std::string_view> words;
            while (!trimmed.empty()) {
                const auto wordEnd = trimmed.find_first_of(" \t");
                words.push_back(trimmed.substr(0, wordEnd));
                if (wordEnd == std::string_view::npos) break;
                trimmed.remove_prefix(wordEnd);
                const auto next = trimmed.find_first_not_of(" \t");
                if (next == std::string_view::npos) break;
                trimmed.remove_prefix(next);
            }
            std::string aligned;
            intptr_t wordsLength = 0;
            for (const auto word : words)
                wordsLength += static_cast<intptr_t>(Scintilla::Internal::UTF16Length(word));
            const auto extraSpaces =
                maximumColumn - minimumIndent - wordsLength - static_cast<intptr_t>(words.size()) + 1;
            const bool paragraph = mode == AlignmentMode::JustifyParagraph;
            const bool nextLineBlank = paragraph && (Send(Message::GetLineCount) <= line + 1 ||
                                                     Send(Message::GetLineIndentPosition, line + 1) ==
                                                         Send(Message::GetLineEndPosition, line + 1));
            const bool justify = (mode == AlignmentMode::Justify || paragraph) && words.size() > 1 &&
                                 wordsLength >= 2 &&
                                 (!paragraph || !nextLineBlank || first == last ||
                                  wordsLength * 4 > (maximumColumn - minimumIndent) * 3);
            const auto gaps = static_cast<intptr_t>(words.size()) - 1;
            const auto spacesPerGap = justify ? (maximumColumn - minimumIndent - wordsLength) / gaps : 1;
            const auto remainder = justify ? (maximumColumn - minimumIndent - wordsLength) % gaps : 0;
            if (mode == AlignmentMode::Right && extraSpaces > 0) aligned.append(static_cast<size_t>(extraSpaces), ' ');
            auto oddSpaces = extraSpaces % 2;
            if (mode == AlignmentMode::Center && extraSpaces - oddSpaces > 0)
                aligned.append(static_cast<size_t>((extraSpaces - oddSpaces) / 2), ' ');
            for (size_t i = 0; i < words.size(); ++i) {
                if (i > 0) {
                    if (spacesPerGap > 0) aligned.append(static_cast<size_t>(spacesPerGap), ' ');
                    if (static_cast<intptr_t>(i) > gaps - remainder) aligned += ' ';
                }
                aligned += words[i];
                if (mode == AlignmentMode::Center && words.size() > 1 && oddSpaces > 0 && i + 1 >= words.size() / 2) {
                    aligned += ' ';
                    --oddSpaces;
                }
            }
            auto targetStart = start;
            if (mode == AlignmentMode::Right || mode == AlignmentMode::Center) {
                Send(Message::SetLineIndentation, line, minimumIndent);
                targetStart = Send(Message::GetLineIndentPosition, line);
            }
            Send(Message::SetTargetRange, targetStart, Send(Message::GetLineEndPosition, line));
            Send(Message::ReplaceTarget, aligned.size(), reinterpret_cast<intptr_t>(aligned.data()));
            if (mode != AlignmentMode::Right && mode != AlignmentMode::Center)
                Send(Message::SetLineIndentation, line, minimumIndent);
        }
        Send(Message::EndUndoAction);
    }
    const auto selectionStart = Send(Message::PositionFromLine, first);
    const auto selectionEnd = Send(Message::PositionFromLine, last + 1);
    Send(Message::SetSel, reversed ? selectionEnd : selectionStart, reversed ? selectionStart : selectionEnd);
    return result;
}
