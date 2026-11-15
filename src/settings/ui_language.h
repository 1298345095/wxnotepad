#pragma once

#include <cstdint>

namespace UILanguage {
inline constexpr uint16_t System = 1024;
inline constexpr uint16_t PrimaryLanguageMask = 0x3ff;
inline constexpr int SublanguageShift = 10;
inline constexpr uint16_t ChinesePrimaryLanguage = 0x04;
inline constexpr uint16_t ChineseTaiwanSublanguage = 1;
inline constexpr uint16_t ChineseHongKongSublanguage = 3;
inline constexpr uint16_t ChineseMacauSublanguage = 5;

// Notepad4 89b8b36 casts to LANGID before validating its primary language.
constexpr uint16_t Normalize(int value) {
    const auto language = static_cast<uint16_t>(value);
    switch (language & PrimaryLanguageMask) {
    case 0x09: // English
    case ChinesePrimaryLanguage:
    case 0x0c: // French
    case 0x07: // German
    case 0x10: // Italian
    case 0x11: // Japanese
    case 0x12: // Korean
    case 0x15: // Polish
    case 0x16: // Portuguese
    case 0x19: // Russian
    case 0x24: // Slovenian
        return language;
    default:
        return System;
    }
}
} // namespace UILanguage
