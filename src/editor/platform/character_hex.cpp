// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
#include "editor/platform/character_hex.h"
#include "editor/ascii_character.h"
#include "editor/platform/utf8_scalar.h"
#include <cstdint>
#include <format>
#include <iterator>

namespace {
void AppendUtf16Scalar(std::u16string& result, uint32_t value) {
    if (value >= 0x10000) {
        result += static_cast<char16_t>(0xd800 + ((value - 0x10000) >> 10));
        value = 0xdc00 + (value & 0x3ff);
    }
    result += static_cast<char16_t>(value);
}
std::u16string Utf16(std::string_view text) {
    std::u16string result;
    for (size_t position = 0; position < text.size();) {
        const auto scalar = WindowsUtf8::ReadScalar(text.substr(position));
        AppendUtf16Scalar(result, scalar.value);
        position += scalar.consumed;
    }
    return result;
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
        const int digit = Ascii::HexDigit(input[end]);
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
        }
        WindowsUtf8::AppendScalar(result, value);
    }
    return result;
}
} // namespace

std::string CharacterToHex(std::string_view text) {
    // EditCharacterToHex at pinned Notepad4 89b8b36: WCHAR formatting on both
    // platforms, including the extra annotation for exactly one surrogate pair.
    const auto input = Utf16(text);
    std::string result;
    for (const uint32_t unit : input) {
        if (unit <= 0xff)
            std::format_to(std::back_inserter(result), "\\x{:02X}", unit);
        else
            std::format_to(std::back_inserter(result), "\\u{:04X}", unit);
    }
    if (input.size() == 2 && input[0] >= 0xd800 && input[0] <= 0xdbff && input[1] >= 0xdc00 && input[1] <= 0xdfff) {
        const uint32_t scalar = 0x10000 + ((input[0] - 0xd800) << 10) + input[1] - 0xdc00;
        std::format_to(std::back_inserter(result), " U+{:06X}", scalar);
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
