// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
#include "editor/editor_control.h"
#include "editor/platform/utf8_scalar.h"
#include "editor/platform/native_encoding.h"
#include <ScintillaStructures.h>
#include <algorithm>
#include <array>

namespace {
std::u16string ExcerptUtf16(std::string_view input) {
    std::u16string text;
    size_t position = 0;
    while (position < input.size()) {
        const auto scalar = WindowsUtf8::ReadScalar(input.substr(position));
        if (scalar.value == 0) break;
        if (scalar.value >= 0x10000) {
            text += static_cast<char16_t>(0xd800 + ((scalar.value - 0x10000) >> 10));
            text += static_cast<char16_t>(0xdc00 + ((scalar.value - 0x10000) & 0x3ff));
        } else {
            text += static_cast<char16_t>(scalar.value);
        }
        position += scalar.consumed;
    }
    return text;
}
} // namespace

wxString EditorControl::CaptureTitleExcerpt(bool defaultEncoding) {
    // Edit.cpp::EditGetExcerpt at Notepad4 89b8b36: preserve the byte bound,
    // UTF-16 character bound, trailing space and delayed ellipsis threshold.
    using enum Scintilla::Message;
    if (Send(GetSelectionEmpty) || Send(SelectionIsRectangle)) return {};
    constexpr intptr_t ExcerptInputBytes = 255;
    constexpr size_t ExcerptBufferUnits = 128;
    // A native byte can occupy up to four UTF-8 bytes in the migrated core.
    constexpr intptr_t MaximumCoreInputBytes = 4 * ExcerptInputBytes;
    const auto start = Send(GetSelectionStart);
    const auto limit = defaultEncoding ? MaximumCoreInputBytes : ExcerptInputBytes;
    const auto end = std::min(Send(GetSelectionEnd), start + limit);
    std::array<char, MaximumCoreInputBytes + 1> bytes{};
    const Scintilla::TextRangeFull range{{start, end}, bytes.data()};
    Send(GetTextRangeFull, 0, reinterpret_cast<intptr_t>(&range));
    std::string input(bytes.data(), static_cast<size_t>(end - start));
    if (defaultEncoding) {
        input = NativeBytesFromRanges(input, start, *RawNativeBytes());
        input.resize(std::min(input.size(), static_cast<size_t>(ExcerptInputBytes)));
        input = NativeDefaultEncodingUnicode(input);
    }
    const auto text = ExcerptUtf16(input);
    std::u16string excerpt;
    char16_t previous = u' ';
    for (auto character : text) {
        if (character <= u' ') {
            character = u' ';
            if (previous == u' ') continue;
        }
        excerpt += character;
        previous = character;
        if (excerpt.size() > ExcerptBufferUnits) {
            excerpt.resize(ExcerptBufferUnits - 4);
            excerpt += u"...";
            break;
        }
    }
    if (excerpt.empty()) return " ... ";
    // Avoid the original out-of-bounds terminator at its 128-unit boundary.
    if (excerpt.size() >= ExcerptBufferUnits) excerpt.resize(ExcerptBufferUnits - 1);
    wxString result;
    for (size_t index = 0; index < excerpt.size(); ++index) {
        uint32_t scalar = excerpt[index];
        if (scalar >= 0xd800 && scalar <= 0xdbff && index + 1 < excerpt.size() && excerpt[index + 1] >= 0xdc00 &&
            excerpt[index + 1] <= 0xdfff)
            scalar = 0x10000 + ((scalar - 0xd800) << 10) + excerpt[++index] - 0xdc00;
        else if (scalar >= 0xd800 && scalar <= 0xdfff)
            scalar = 0xfffd;
        result += wxUniChar(scalar);
    }
    return result;
}
