#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <UniConversion.h>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace WindowsUtf8 {
struct Scalar {
    uint32_t value;
    size_t consumed;
    bool valid;
};

// Read a nonempty UTF-8 prefix with MultiByteToWideChar(CP_UTF8, 0) semantics.
// A scalar range violation consumes the lead byte and first continuation byte.
// An incomplete sequence stops before the next non-continuation byte.
inline Scalar ReadScalar(std::string_view text) {
    const auto first = static_cast<unsigned char>(text.front());
    if (first < 0x80) return {first, 1, true};
    const size_t width = first >= 0xc2 && first <= 0xdf   ? 2
                         : first >= 0xe0 && first <= 0xef ? 3
                         : first >= 0xf0 && first <= 0xf4 ? 4
                                                          : 0;
    uint32_t value = width ? first & (0x7f >> width) : 0xfffd;
    size_t consumed = 1;
    bool valid = width != 0;
    while (valid && consumed < width) {
        if (consumed == text.size() || (static_cast<unsigned char>(text[consumed]) & 0xc0) != 0x80) {
            valid = false;
            break;
        }
        const auto next = static_cast<unsigned char>(text[consumed++]);
        value = (value << 6) | (next & 0x3f);
        if (consumed == 2 && ((first == 0xe0 && next < 0xa0) || (first == 0xed && next >= 0xa0) ||
                              (first == 0xf0 && next < 0x90) || (first == 0xf4 && next >= 0x90)))
            valid = false;
    }
    return {valid ? value : 0xfffd, consumed, valid};
}

// Append a UTF-16-derived scalar, replacing lone surrogates as Win32 does.
inline void AppendScalar(std::string& output, uint32_t value) {
    using namespace Scintilla::Internal;
    if (IsSurrogate(value)) value = unicodeReplacementChar;
    char bytes[UTF8MaxBytes + 1];
    UTF8FromUTF32Character(static_cast<int>(value), bytes);
    // The core encoder writes a terminator; retain U+0000 as one data byte.
    output.append(bytes, static_cast<size_t>(UTF8BytesOfLead(static_cast<unsigned char>(bytes[0]))));
}
} // namespace WindowsUtf8
