#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <UniConversion.h>
#include <string>

namespace {
void AppendWordBoundary(std::wstring& converted, int nextWord, int column, const wchar_t* eol, int& lineLength,
                        bool& modified) {
    if (nextWord <= 0) return;
    if (lineLength + nextWord + 1 > column) {
        converted += eol;
        lineLength = 0;
        modified = true;
    } else if (lineLength > 0) {
        converted += L' ';
        ++lineLength;
    }
}

bool WrapUnits(const std::wstring& text, size_t count, int column, const wchar_t* eol, std::wstring& converted) {
    const auto white = [](wchar_t ch) { return ch == L' ' || ch == L'\t'; };
    const auto space = [](wchar_t ch) { return ch == L' ' || (ch >= 9 && ch <= 13); };
    int lineLength = 0;
    bool modified = false;
    // Edit.cpp::EditWrapToColumn at 89b8b36 counts UTF-16 units, preserving
    // existing EOLs and emitting the current EOL only when inserting a break.
    for (size_t index = 0; index < count; ++index) {
        const auto ch = text[index];
        if (!white(ch)) {
            converted += ch;
            if (ch == L'\r' || ch == L'\n')
                lineLength = 0;
            else
                ++lineLength;
            continue;
        }
        while (white(text[index + 1])) {
            ++index;
            modified = true;
        }
        size_t wordEnd = index + 1;
        while (wordEnd < count && text[wordEnd] && !space(text[wordEnd]))
            ++wordEnd;
        AppendWordBoundary(converted, static_cast<int>(wordEnd - index - 1), column, eol, lineLength, modified);
    }
    return modified;
}

std::string WrappedUtf8(const std::wstring& converted) {
    using namespace Scintilla::Internal;
    std::string replacement;
    // These core UTF-16 helpers stop at NUL. Convert each segment so the
    // original explicit-length WideCharToMultiByte behavior is retained.
    std::wstring_view remaining(converted);
    while (!remaining.empty()) {
        const auto nul = remaining.find(L'\0');
        const auto segment = remaining.substr(0, nul);
        const auto offset = replacement.size();
        const auto byteLength = UTF8Length(segment);
        replacement.resize(offset + byteLength);
        UTF8FromUTF16(segment, replacement.data() + offset, byteLength);
        if (nul == std::wstring_view::npos) break;
        replacement += '\0';
        remaining.remove_prefix(nul + 1);
    }
    return replacement;
}
} // namespace

void EditorControl::SplitLines() {
    // Notepad4 89b8b36 Notepad4.cpp::IDM_EDIT_SPLITLINES. The core measures
    // the current rendered view and uses the main selection as its target.
    Send(Scintilla::Message::TargetFromSelection);
    Send(Scintilla::Message::LinesSplit, 0);
}

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
bool EditorControl::WrapToColumn(int column) {
    using Scintilla::Message;
    using namespace Scintilla::Internal;
    if (Send(Message::GetSelectionEmpty)) return true;
    if (Send(Message::SelectionIsRectangle)) return false;
    const auto start =
        Send(Message::PositionFromLine, Send(Message::LineFromPosition, Send(Message::GetSelectionStart)));
    const auto end = Send(Message::GetSelectionEnd);
    const auto length = end - start;
    const auto* bytes = reinterpret_cast<const char*>(Send(Message::GetRangePointer, start, length));
    const std::string_view utf8(bytes, static_cast<size_t>(length));
    const auto count = UTF16Length(utf8);
    std::wstring text(count + 1, L'\0');
    UTF16FromUTF8(utf8, text.data(), count);
    const auto mode = static_cast<Scintilla::EndOfLine>(Send(Message::GetEOLMode));
    const auto eol = mode == Scintilla::EndOfLine::CrLf ? L"\r\n" : mode == Scintilla::EndOfLine::Cr ? L"\r" : L"\n";
    std::wstring converted;
    // The original applies normalization only when a whitespace run was
    // collapsed or a break inserted, even if the converted bytes are equal.
    if (WrapUnits(text, count, column, eol, converted)) {
        const auto replacement = WrappedUtf8(converted);
        const bool reversed = Send(Message::GetAnchor) > Send(Message::GetCurrentPos);
        Send(Message::SetTargetRange, start, end);
        Send(Message::ReplaceTarget, replacement.size(), reinterpret_cast<intptr_t>(replacement.data()));
        const auto replacementEnd = start + static_cast<intptr_t>(replacement.size());
        Send(Message::SetSel, reversed ? replacementEnd : start, reversed ? start : replacementEnd);
    }
    return true;
}
