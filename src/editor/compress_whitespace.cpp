#include "editor/editor_control.h"
#include <string>

// Adapted from Notepad4 89b8b36 Edit.cpp::EditCompressSpaces and EditReplaceRange.
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
bool EditorControl::CompressWhitespace() {
    using Scintilla::Message;
    if (Send(Message::SelectionIsRectangle)) return false;

    auto start = Send(Message::GetSelectionStart);
    auto end = Send(Message::GetSelectionEnd);
    const auto length = Send(Message::GetLength);
    char previous = '\n';
    char next = '\n';
    const bool wholeDocument = start == end;
    if (wholeDocument) {
        start = 0;
        end = length;
    } else {
        if (start != 0) previous = static_cast<char>(Send(Message::GetCharAt, start - 1));
        if (end < length) next = static_cast<char>(Send(Message::GetCharAt, end));
    }

    const auto count = static_cast<size_t>(end - start);
    std::string buffer(count + 2, '\0');
    Send(wholeDocument ? Message::GetText : Message::GetSelText, wholeDocument ? count + 1 : 0,
         reinterpret_cast<intptr_t>(buffer.data()));
    buffer[count] = next;
    const char* const limit = buffer.data() + count;
    char* output = buffer.data();
    bool modified = false;
    for (const char* input = buffer.data(); input < limit;) {
        const char ch = *input++;
        if (ch != ' ' && ch != '\t') {
            previous = ch;
            *output++ = ch;
        } else if (previous != '\n' && previous != '\r') {
            while (input < limit && (*input == ' ' || *input == '\t'))
                ++input;
            if (*input != '\n' && *input != '\r') {
                previous = ' ';
                *output++ = ' ';
                if (ch == '\t') modified = true;
            }
        }
    }
    if (modified || output < limit) {
        const auto resultLength = static_cast<intptr_t>(output - buffer.data());
        const bool backwards = Send(Message::GetAnchor) > Send(Message::GetCurrentPos);
        Send(Message::SetTargetRange, start, end);
        Send(Message::ReplaceTarget, resultLength, reinterpret_cast<intptr_t>(buffer.data()));
        Send(Message::SetSel, backwards ? start + resultLength : start, backwards ? start : start + resultLength);
    }
    return true;
}
