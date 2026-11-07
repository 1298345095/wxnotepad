#pragma once
#include "app/dialogs/column_wrap_digits.h"
#include <wx/string.h>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>

inline int ColumnInputDigit(char32_t character) {
    // Captured from the original dialogs' Windows GetDlgItemInt conversion.
    for (const auto& range : ColumnWrapDigitRanges)
        if (character >= range.first && character <= range.last) return static_cast<int>(character - range.zero);
    return -1;
}

inline std::optional<int> ReadColumnInput(const wxString& text) {
    std::string digits;
    for (auto position = text.begin(); position != text.end(); ++position) {
        auto character = static_cast<char32_t>(wxUniChar(*position).GetValue());
        // wxString uses UTF-16 units on Windows, including supplementary digits.
        if (character >= 0xD800 && character <= 0xDBFF) {
            if (++position == text.end()) return std::nullopt;
            const auto low = wxUniChar(*position).GetValue();
            if (low < 0xDC00 || low > 0xDFFF) return std::nullopt;
            character = 0x10000 + ((character - 0xD800) << 10) + low - 0xDC00;
        }
        if (character == ' ' && digits.empty()) continue;
        const auto digit = ColumnInputDigit(character);
        if (digit < 0) return std::nullopt;
        digits += static_cast<char>('0' + digit);
    }
    if (digits.empty()) return std::nullopt;
    uint32_t number = 0;
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) return std::nullopt;
    // Original unsigned GetDlgItemInt stores its UINT result in a signed int.
    return static_cast<int32_t>(number);
}
