#include "editor/editor_control.h"
#include <ScintillaTypes.h>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
struct ModifyLinesText {
    std::string prefix;
    std::string suffix;
    intptr_t lineCount = 0;
    intptr_t number = 0;
    size_t width = 1;
    char substitution = '\0';
    bool padZero = false;

    ModifyLinesText(const wxString& text, intptr_t firstLine, intptr_t lastLine, Scintilla::EndOfLine mode) {
        prefix = text.ToStdString(wxConvUTF8);
        prefix.resize(prefix.find('\0') == std::string::npos ? prefix.size() : prefix.find('\0'));
        // Original ConvertWinEditLineEndingsEx converts CRLF pairs only and
        // counts every separator, including lone CR and LF.
        size_t output = 0;
        for (size_t input = 0; input < prefix.size(); ++input) {
            char value = prefix[input];
            if (value == '\r') {
                ++lineCount;
                if (input + 1 < prefix.size() && prefix[input + 1] == '\n') {
                    if (mode == Scintilla::EndOfLine::CrLf) {
                        prefix[output++] = value;
                        value = '\n';
                    } else {
                        value = mode == Scintilla::EndOfLine::Lf ? '\n' : '\r';
                    }
                    ++input;
                }
            } else if (value == '\n') {
                ++lineCount;
            }
            prefix[output++] = value;
        }
        prefix.resize(output);
        size_t position = 0;
        while ((position = prefix.find("$(", position)) != std::string::npos) {
            auto token = position + 2;
            const bool zero = token < prefix.size() && prefix[token] == '0';
            if (zero) ++token;
            if (token + 1 < prefix.size() && (prefix[token] == 'L' || prefix[token] == 'N' || prefix[token] == 'I') &&
                prefix[token + 1] == ')') {
                substitution = prefix[token];
                padZero = zero;
                number = substitution == 'N';
                auto count = substitution == 'L' ? lastLine + 1 : lastLine - firstLine + number;
                while (count >= 10) {
                    ++width;
                    count /= 10;
                }
                suffix = prefix.substr(token + 2);
                prefix.resize(position);
                break;
            }
            position = token;
        }
    }

    std::string InsertText(intptr_t line) {
        if (!substitution) return prefix;
        auto digits = std::to_string(substitution == 'L' ? line + 1 : number);
        if (digits.size() < width) digits.insert(0, width - digits.size(), padZero ? '0' : ' ');
        ++number;
        return prefix + digits + suffix;
    }
};
} // namespace

bool EditorControl::ModifyLines(const wxString& before, const wxString& after, bool skipEmpty) {
    using Scintilla::Message;
    // Adapted from Notepad4 89b8b36 Edit.cpp::EditModifyLines.
    if (Send(Message::SelectionIsRectangle)) return false;
    if (before.empty() && after.empty()) return true;
    const auto start = Send(Message::GetSelectionStart);
    const auto end = Send(Message::GetSelectionEnd);
    const auto firstLine = Send(Message::LineFromPosition, start);
    auto lastLine = Send(Message::LineFromPosition, end);
    if (end <= Send(Message::PositionFromLine, lastLine) && lastLine > firstLine) --lastLine;
    const auto mode = static_cast<Scintilla::EndOfLine>(Send(Message::GetEOLMode));
    ModifyLinesText prefix(before, firstLine, lastLine, mode);
    ModifyLinesText suffix(after, firstLine, lastLine, mode);
    const auto insert = [&](ModifyLinesText& field, intptr_t line, intptr_t position) {
        const auto text = field.InsertText(line);
        Send(Message::SetTargetRange, position, position);
        Send(Message::ReplaceTarget, text.size(), reinterpret_cast<intptr_t>(text.c_str()));
    };
    Send(Message::BeginUndoAction);
    for (auto line = firstLine, destination = firstLine; line <= lastLine; ++line, ++destination) {
        const auto lineStart = Send(Message::PositionFromLine, destination);
        auto lineEnd = Send(Message::GetLineEndPosition, destination);
        if (skipEmpty && lineStart == lineEnd) continue;
        if (!before.empty()) {
            destination += prefix.lineCount;
            insert(prefix, line, lineStart);
        }
        if (!after.empty()) {
            if (!before.empty()) lineEnd = Send(Message::GetLineEndPosition, destination);
            destination += suffix.lineCount;
            insert(suffix, line, lineEnd);
        }
    }
    Send(Message::EndUndoAction);
    if (start != end) {
        const auto anchor = Send(Message::PositionFromLine, firstLine);
        const auto caret = Send(Message::PositionFromLine, lastLine + 1);
        if (Send(Message::GetCurrentPos) < Send(Message::GetAnchor))
            Send(Message::SetSel, caret, anchor);
        else
            Send(Message::SetSel, anchor, caret);
    }
    return true;
}
