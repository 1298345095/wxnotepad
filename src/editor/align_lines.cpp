#include "editor/editor_control.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
#include <UniConversion.h>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.

namespace {
using Scintilla::Message;
struct AlignmentColumns {
    intptr_t minimumIndent = 1024;
    intptr_t maximumColumn = 0;
};

AlignmentColumns MeasureLines(EditorControl& editor, intptr_t first, intptr_t last) {
    AlignmentColumns columns;
    for (auto line = first; line <= last; ++line) {
        auto lineEnd = editor.Send(Message::GetLineEndPosition, line);
        const auto indent = editor.Send(Message::GetLineIndentPosition, line);
        if (indent >= lineEnd) continue;
        while (lineEnd > indent) {
            const auto ch = editor.Send(Message::GetCharAt, lineEnd - 1);
            if (ch != ' ' && ch != '\t') break;
            --lineEnd;
        }
        columns.minimumIndent = std::min(columns.minimumIndent, editor.Send(Message::GetLineIndentation, line));
        columns.maximumColumn = std::max(columns.maximumColumn, editor.Send(Message::GetColumn, lineEnd, line));
    }
    return columns;
}

std::vector<std::string_view> AlignmentWords(const std::string& text) {
    std::string_view trimmed(text.c_str());
    const auto firstWord = trimmed.find_first_not_of("\r\n\t ");
    if (firstWord == std::string_view::npos) return {};
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
    return words;
}

struct AlignmentSpacing {
    intptr_t extra;
    intptr_t gaps;
    intptr_t perGap;
    intptr_t remainder;
};

AlignmentSpacing MeasureSpacing(const std::vector<std::string_view>& words, AlignmentMode mode, intptr_t width,
                                bool nextLineBlank, bool singleLine) {
    intptr_t wordsLength = 0;
    for (const auto word : words)
        wordsLength += static_cast<intptr_t>(Scintilla::Internal::UTF16Length(word));
    const auto extraSpaces = width - wordsLength - static_cast<intptr_t>(words.size()) + 1;
    const bool paragraph = mode == AlignmentMode::JustifyParagraph;
    const bool justify = (mode == AlignmentMode::Justify || paragraph) && words.size() > 1 && wordsLength >= 2 &&
                         (!paragraph || !nextLineBlank || singleLine || wordsLength * 4 > width * 3);
    const auto gaps = static_cast<intptr_t>(words.size()) - 1;
    return {extraSpaces, gaps, justify ? (width - wordsLength) / gaps : 1, justify ? (width - wordsLength) % gaps : 0};
}

std::string AlignWords(const std::vector<std::string_view>& words, AlignmentMode mode, AlignmentSpacing spacing) {
    std::string aligned;
    if (mode == AlignmentMode::Right && spacing.extra > 0) aligned.append(static_cast<size_t>(spacing.extra), ' ');
    auto oddSpaces = spacing.extra % 2;
    if (mode == AlignmentMode::Center && spacing.extra - oddSpaces > 0)
        aligned.append(static_cast<size_t>((spacing.extra - oddSpaces) / 2), ' ');
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) {
            if (spacing.perGap > 0) aligned.append(static_cast<size_t>(spacing.perGap), ' ');
            if (static_cast<intptr_t>(i) > spacing.gaps - spacing.remainder) aligned += ' ';
        }
        aligned += words[i];
        if (mode == AlignmentMode::Center && words.size() > 1 && oddSpaces > 0 && i + 1 >= words.size() / 2) {
            aligned += ' ';
            --oddSpaces;
        }
    }
    return aligned;
}

void AlignLine(EditorControl& editor, intptr_t line, AlignmentMode mode, AlignmentColumns columns, bool singleLine) {
    const auto start = editor.Send(Message::PositionFromLine, line);
    const auto lineEnd = editor.Send(Message::GetLineEndPosition, line);
    const auto indent = editor.Send(Message::GetLineIndentPosition, line);
    if (indent == lineEnd && lineEnd > 0) {
        editor.Send(Message::DeleteRange, start, lineEnd - start);
        return;
    }
    const auto length = editor.Send(Message::LineLength, line);
    const auto* bytes = reinterpret_cast<const char*>(editor.Send(Message::GetRangePointer, start, length));
    const std::string text(bytes ? bytes : "", static_cast<size_t>(length));
    // The original fixed UTF-16 conversion fails for an overlong input,
    // including line endings, even when its trimmed column fits.
    if (Scintilla::Internal::UTF16Length(text) > 1024) return;
    const auto words = AlignmentWords(text);
    if (words.empty()) return;
    const bool nextLineBlank =
        mode == AlignmentMode::JustifyParagraph &&
        (editor.Send(Message::GetLineCount) <= line + 1 ||
         editor.Send(Message::GetLineIndentPosition, line + 1) == editor.Send(Message::GetLineEndPosition, line + 1));
    const auto spacing =
        MeasureSpacing(words, mode, columns.maximumColumn - columns.minimumIndent, nextLineBlank, singleLine);
    const auto aligned = AlignWords(words, mode, spacing);
    auto targetStart = start;
    if (mode == AlignmentMode::Right || mode == AlignmentMode::Center) {
        editor.Send(Message::SetLineIndentation, line, columns.minimumIndent);
        targetStart = editor.Send(Message::GetLineIndentPosition, line);
    }
    editor.Send(Message::SetTargetRange, targetStart, editor.Send(Message::GetLineEndPosition, line));
    editor.Send(Message::ReplaceTarget, aligned.size(), reinterpret_cast<intptr_t>(aligned.data()));
    if (mode != AlignmentMode::Right && mode != AlignmentMode::Center)
        editor.Send(Message::SetLineIndentation, line, columns.minimumIndent);
}
} // namespace

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
    const auto columns = MeasureLines(*this, first, last);
    const auto result = columns.maximumColumn < 1024 ? AlignmentResult::Complete : AlignmentResult::LinesTooLong;
    if (result == AlignmentResult::Complete) {
        Send(Message::BeginUndoAction);
        for (auto line = first; line <= last; ++line)
            AlignLine(*this, line, mode, columns, first == last);
        Send(Message::EndUndoAction);
    }
    const auto selectionStart = Send(Message::PositionFromLine, first);
    const auto selectionEnd = Send(Message::PositionFromLine, last + 1);
    Send(Message::SetSel, reversed ? selectionEnd : selectionStart, reversed ? selectionStart : selectionEnd);
    return result;
}
