#pragma once

#include <cstdint>
#include <string>
#include <ScintillaTypes.h>

// Original ConvertWinEditLineEndingsEx converts CRLF pairs only and counts
// every separator, including lone CR and LF. Callers own any NUL truncation.
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
inline intptr_t ConvertWinEditLineEndings(std::string& text, Scintilla::EndOfLine mode) {
    intptr_t lineCount = 0;
    size_t output = 0;
    for (size_t input = 0; input < text.size(); ++input) {
        char value = text[input];
        if (value != '\r') {
            if (value == '\n') ++lineCount;
            text[output++] = value;
            continue;
        }
        ++lineCount;
        if (input + 1 < text.size() && text[input + 1] == '\n') {
            if (mode == Scintilla::EndOfLine::CrLf) {
                text[output++] = value;
                value = '\n';
            } else {
                value = mode == Scintilla::EndOfLine::Lf ? '\n' : '\r';
            }
            ++input;
        }
        text[output++] = value;
    }
    text.resize(output);
    return lineCount;
}
