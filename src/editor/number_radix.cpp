#include "editor/editor_control.h"
#include <charconv>
#include <string>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
using Scintilla::Message;
bool IsAlphaNumeric(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}
int GetHexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = static_cast<char>(ch | 0x20);
    return ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}
void ConvertNumRadix(std::string& output, uint64_t num, unsigned radix) {
    // Adapted from Notepad4 89b8b36 Edit.cpp::ConvertNumRadix.
    if (radix == 16 || radix == 10) {
        char buffer[32]{};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), num, static_cast<int>(radix));
        if (radix == 16) output += "0x";
        output.append(buffer, result.ptr);
    } else {
        char buffer[80]{};
        unsigned index = sizeof(buffer) - 1;
        unsigned bitCount = 0;
        const unsigned shift = radix == 8 ? 3 : 1;
        do {
            buffer[--index] = static_cast<char>('0' + (num & (radix - 1)));
            num >>= shift;
            ++bitCount;
            if (radix == 2 && num && (bitCount & 7) == 0) buffer[--index] = '_';
        } while (num);
        buffer[--index] = radix == 8 ? 'O' : 'b';
        buffer[--index] = '0';
        output += buffer + index;
    }
}
} // namespace

bool EditorControl::ConvertNumberRadix(unsigned radix) {
    const auto length = Send(Message::GetSelText);
    if (length == 0) return true;
    if (Send(Message::SelectionIsRectangle)) return false;
    std::string input(static_cast<size_t>(length) + 1, '\0');
    Send(Message::GetSelText, 0, reinterpret_cast<intptr_t>(input.data()));
    std::string output;
    // Preserve the pinned EditConvertNumRadix scanner, including unsigned wrap,
    // incomplete prefixes, underscores and the already-matching radix shortcut.
    const char* p = input.c_str();
    uint64_t value = 0;
    while (*p) {
        if (*p == '0') {
            value = 0;
            ++p;
            const char prefix = static_cast<char>(*p | 0x20);
            if (prefix == 'x' && radix != 16) {
                ++p;
                while (*p) {
                    if (*p == '_') {
                        ++p;
                    } else {
                        const int hex = GetHexDigit(*p);
                        if (hex < 0) break;
                        value = (value << 4) | static_cast<unsigned>(hex);
                        ++p;
                    }
                }
                ConvertNumRadix(output, value, radix);
            } else if (prefix == 'o' && radix != 8) {
                ++p;
                while (*p) {
                    if (*p >= '0' && *p <= '7') {
                        value <<= 3;
                        value += (*p++ - '0');
                    } else if (*p == '_') {
                        ++p;
                    } else {
                        break;
                    }
                }
                ConvertNumRadix(output, value, radix);
            } else if (prefix == 'b' && radix != 2) {
                ++p;
                while (*p) {
                    if (*p == '0') {
                        value <<= 1;
                        ++p;
                    } else if (*p == '1') {
                        value <<= 1;
                        value |= 1;
                        ++p;
                    } else if (*p == '_') {
                        ++p;
                    } else {
                        break;
                    }
                }
                ConvertNumRadix(output, value, radix);
            } else if ((*p >= '0' && *p <= '9') && radix != 10) {
                value = *p++ - '0';
                while (*p) {
                    if (*p >= '0' && *p <= '9') {
                        value *= 10;
                        value += (*p++ - '0');
                    } else if (*p == '_') {
                        ++p;
                    } else {
                        break;
                    }
                }
                ConvertNumRadix(output, value, radix);
            } else {
                output += '0';
            }
        } else if ((*p >= '1' && *p <= '9') && radix != 10) {
            value = *p++ - '0';
            while (*p) {
                if (*p >= '0' && *p <= '9') {
                    value *= 10;
                    value += (*p++ - '0');
                } else if (*p == '_') {
                    ++p;
                } else {
                    break;
                }
            }
            ConvertNumRadix(output, value, radix);
        } else if (IsAlphaNumeric(*p) || *p == '_') {
            do {
                output += *p++;
            } while (IsAlphaNumeric(*p) || *p == '_');
        } else {
            output += *p++;
        }
    }
    // Original EditReplaceMainSelection: GetSelText may concatenate multiple
    // selections, but replacement and direction use only the main selection.
    const auto start = Send(Message::GetSelectionStart);
    const auto end = Send(Message::GetSelectionEnd);
    const bool backwards = Send(Message::GetAnchor) > Send(Message::GetCurrentPos);
    Send(Message::SetTargetStart, start);
    Send(Message::SetTargetEnd, end);
    Send(Message::ReplaceTarget, output.size(), reinterpret_cast<intptr_t>(output.data()));
    const auto resultEnd = start + static_cast<intptr_t>(output.size());
    Send(Message::SetSel, backwards ? resultEnd : start, backwards ? start : resultEnd);
    return true;
}
