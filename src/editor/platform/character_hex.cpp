// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
#include "editor/platform/character_hex.h"
#include <cstdint>
#include <cstdio>

namespace {
bool InvalidScalarPrefix(unsigned char first, unsigned char next) {
    return (first == 0xe0 && next < 0xa0) || (first == 0xed && next >= 0xa0) || (first == 0xf0 && next < 0x90) ||
           (first == 0xf4 && next >= 0x90);
}
uint32_t ReadUtf8Scalar(std::string_view text, size_t& position) {
    const auto first = static_cast<unsigned char>(text[position++]);
    if (first < 0x80) return first;
    const int width = first >= 0xc2 && first <= 0xdf   ? 2
                      : first >= 0xe0 && first <= 0xef ? 3
                      : first >= 0xf0 && first <= 0xf4 ? 4
                                                       : 0;
    uint32_t value = width ? first & (0x7f >> width) : 0xfffd;
    bool valid = width != 0;
    for (int index = 1; valid && index < width; ++index) {
        if (position == text.size() || (static_cast<unsigned char>(text[position]) & 0xc0) != 0x80) {
            valid = false;
            break;
        }
        const auto next = static_cast<unsigned char>(text[position++]);
        value = (value << 6) | (next & 0x3f);
        if (index == 1 && InvalidScalarPrefix(first, next)) valid = false;
    }
    return valid ? value : 0xfffd;
}
void AppendUtf16Scalar(std::u16string& result, uint32_t value) {
    if (value >= 0x10000) {
        result += static_cast<char16_t>(0xd800 + ((value - 0x10000) >> 10));
        value = 0xdc00 + (value & 0x3ff);
    }
    result += static_cast<char16_t>(value);
}
std::u16string Utf16(std::string_view text) {
    // Match MultiByteToWideChar(CP_UTF8, 0): consume an invalid continuation
    // prefix, including the second byte when its scalar range is invalid.
    std::u16string result;
    for (size_t position = 0; position < text.size();) {
        AppendUtf16Scalar(result, ReadUtf8Scalar(text, position));
    }
    return result;
}
int HexDigit(char16_t character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    return -1;
}
bool ReadHexEscape(std::u16string_view input, size_t& position, uint32_t& unit) {
    if (position == input.size()) return false;
    if (!((unit == '\\' && (input[position] == 'x' || input[position] == 'u' || input[position] == 'U')) ||
          (unit == 'U' && input[position] == '+')))
        return false;
    const auto form = input[position];
    const unsigned minimum = form == 'x' ? 2 : form == 'U' ? 8 : 4;
    const unsigned maximum = form == 'U' ? 8 : form == '+' ? 6 : 4;
    size_t end = position + 1;
    uint32_t value = 0;
    unsigned count = 0;
    while (end < input.size() && count < maximum) {
        const int digit = HexDigit(input[end]);
        if (digit < 0) break;
        value = (value << 4) | digit;
        ++end;
        ++count;
    }
    if (count & 1) {
        --end;
        --count;
        value >>= 4;
    }
    if (count < minimum || value > 0x10ffff) return false;
    position = end;
    unit = value;
    return true;
}
std::string Utf8(std::u16string_view text) {
    std::string result;
    for (size_t index = 0; index < text.size(); ++index) {
        uint32_t value = text[index];
        if (value >= 0xd800 && value <= 0xdbff && index + 1 < text.size() && text[index + 1] >= 0xdc00 &&
            text[index + 1] <= 0xdfff) {
            value = 0x10000 + ((value - 0xd800) << 10) + text[++index] - 0xdc00;
        } else if (value >= 0xd800 && value <= 0xdfff) {
            value = 0xfffd;
        }
        if (value < 0x80) {
            result += static_cast<char>(value);
        } else {
            if (value >= 0x10000) {
                result += static_cast<char>(0xf0 | (value >> 18));
                result += static_cast<char>(0x80 | ((value >> 12) & 0x3f));
            } else if (value >= 0x800) {
                result += static_cast<char>(0xe0 | (value >> 12));
            } else {
                result += static_cast<char>(0xc0 | (value >> 6));
            }
            if (value >= 0x800) result += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (value & 0x3f));
        }
    }
    return result;
}
} // namespace

std::string CharacterToHex(std::string_view text) {
    // EditCharacterToHex at pinned Notepad4 89b8b36: WCHAR formatting on both
    // platforms, including the extra annotation for exactly one surrogate pair.
    const auto input = Utf16(text);
    std::string result;
    char buffer[16];
    for (const uint32_t unit : input) {
        std::snprintf(buffer, sizeof(buffer), unit <= 0xff ? "\\x%02X" : "\\u%04X", unit);
        result += buffer;
    }
    if (input.size() == 2 && input[0] >= 0xd800 && input[0] <= 0xdbff && input[1] >= 0xdc00 && input[1] <= 0xdfff) {
        const uint32_t scalar = 0x10000 + ((input[0] - 0xd800) << 10) + input[1] - 0xdc00;
        std::snprintf(buffer, sizeof(buffer), " U+%06X", scalar);
        result += buffer;
    }
    return result;
}

std::string HexToCharacter(std::string_view text) {
    const auto input = Utf16(text);
    std::u16string result;
    bool changed = false;
    for (size_t position = 0; position < input.size();) {
        uint32_t unit = input[position++];
        if (ReadHexEscape(input, position, unit)) changed = true;
        AppendUtf16Scalar(result, unit);
    }
    return changed ? Utf8(result) : std::string(text);
}
