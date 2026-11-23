#include "editor/editor_control.h"
#include "editor/editor_ids.h"
#include <utility>
#include <ScintillaTypes.h>
#include <SciLexer.h>
#include <algorithm>
#include <string>
#include <string_view>

using Scintilla::Message;

namespace {
std::string IndentPadding(EditorControl& editor) {
    using enum Scintilla::Message;
    auto padding = editor.Send(GetIndent);
    char character = ' ';
    if (editor.Send(GetTabIndents)) {
        if (!editor.Send(GetUseTabs))
            padding = editor.Send(GetTabWidth);
        else {
            padding = 1;
            character = '\t';
        }
    }
    return std::string(static_cast<size_t>(padding), character);
}

void AppendClosingLine(EditorControl& editor, intptr_t line, bool delimiter, std::string_view leadingWhitespace,
                       std::string_view keywordEnd, std::string& insertion) {
    using enum Scintilla::Message;
    const auto next = editor.Send(GetCharAt, editor.Send(PositionFromLine, line));
    if (!keywordEnd.empty() || (delimiter && (next == '}' || next == ']' || next == ')'))) {
        const auto eol = static_cast<Scintilla::EndOfLine>(editor.Send(GetEOLMode));
        insertion += eol == Scintilla::EndOfLine::Lf ? "\n" : eol == Scintilla::EndOfLine::Cr ? "\r" : "\r\n";
        insertion += leadingWhitespace;
        insertion += keywordEnd;
    }
}

// Pinned EditAutoC.cpp::EditKeywordIndent, NP2LEX_MAKEFILE branch. The original
// compares the first case-sensitive alphabetic word, bounded by its 16-byte buffer.
std::string_view MakeKeywordEnd(std::string_view head) {
    constexpr size_t MaximumKeywordLength = 15;
    const auto length =
        std::min(head.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"), MaximumKeywordLength);
    const auto word = head.substr(0, length);
    if (word == "if") return "endif";
    if (word == "define") return "endef";
    if (word == "for") return "endfor";
    return {};
}
} // namespace

void EditorControl::IndentLines(bool forwards) {
    if (!Send(Message::GetReadOnly) && Send(Message::GetSelectionEmpty) && Send(Message::GetSelections) == 1 &&
        Send(Message::GetSelectionMode) ==
            static_cast<intptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream))) {
        // The core excludes an endpoint at a line start. Move an empty
        // selection to its line end so the explicit line command includes it.
        const auto line = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
        Send(Message::GotoPos, Send(Message::GetLineEndPosition, line));
    }
    Send(forwards ? Message::LineIndent : Message::LineDedent);
}

// Adapted from Notepad4 EditAutoC.cpp::EditAutoIndent, pinned 89b8b36.
// Copyright and license: licenses/Notepad4.txt. Delimiter/colon/comment rules
// are scheme independent; Makefile also has the original keyword/fold path.
void EditorControl::AutoIndent() {
    using Scintilla::Message;
    const auto position = Send(Message::GetCurrentPos);
    const auto line = Send(Message::LineFromPosition, position);
    if (line <= 0) return;
    const auto previousStart = Send(Message::PositionFromLine, line - 1);
    const auto previousEnd = Send(Message::GetLineEndPosition, line - 1);
    if (previousEnd == previousStart && (Send(Message::MarkerGet, line - 1) & EditorMarker::BookmarkMask)) {
        Send(Message::MarkerDelete, line - 1, EditorMarker::Bookmark);
        Send(Message::MarkerAdd, line, EditorMarker::Bookmark);
    }
    const auto length = Send(Message::LineLength, line - 1);
    if (length < 2) return;
    std::string previous(static_cast<size_t>(length) + 1, '\0');
    Send(Message::GetLine, line - 1, reinterpret_cast<intptr_t>(previous.data()));
    const auto contentLength = static_cast<size_t>(previousEnd - previousStart);
    const char last = contentLength ? previous[contentLength - 1] : '\0';
    const bool delimiter = last == '{' || last == '[' || last == '(';
    const bool comment = (last == '*' || last == '!') && contentLength >= 3 && previous[contentLength - 3] == '/' &&
                         previous[contentLength - 2] == '*';
    bool indent = delimiter || last == ':' || comment;
    const auto first = previous.find_first_not_of(" \t");
    std::string insertion = previous.substr(0, first);
    const auto leading = static_cast<intptr_t>(insertion.size());
    std::string_view keywordEnd;
    if (!indent && completionStyles_.lexer == SCLEX_MAKEFILE && first < contentLength &&
        Send(Message::GetStyleIndexAt, previousStart + leading) == completionStyles_.autoIndentWordStyle) {
        keywordEnd = MakeKeywordEnd(std::string_view(previous).substr(first, contentLength - first));
        indent = !keywordEnd.empty();
        if (indent && !(Send(Message::GetFoldLevel, line) & std::to_underlying(Scintilla::FoldLevel::HeaderFlag))) {
            const auto parent = Send(Message::GetFoldParent, line);
            if (parent >= 0 && parent + 1 == line) {
                const auto child = Send(Message::GetLastChild, parent, -1);
                if (Send(Message::LineLength, child)) keywordEnd = {};
            } else {
                indent = false;
                keywordEnd = {};
            }
        }
    }
    auto caret = position + leading;
    if (indent) {
        if (comment) {
            insertion += " *";
            caret += 2;
        } else {
            const auto padding = IndentPadding(*this);
            insertion += padding;
            caret += static_cast<intptr_t>(padding.size());
        }
        AppendClosingLine(*this, line, delimiter, std::string_view(previous.data(), static_cast<size_t>(leading)),
                          keywordEnd, insertion);
    }
    if (!insertion.empty()) {
        Send(Message::AddText, insertion.size(), reinterpret_cast<intptr_t>(insertion.data()));
        if (indent) Send(Message::SetSel, caret, caret);
    }
}
