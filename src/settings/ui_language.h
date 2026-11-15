#pragma once

#include <cstdint>

namespace UILanguage {
inline constexpr uint16_t System = 1024;

// Notepad4 89b8b36 casts to LANGID before validating its primary language.
constexpr uint16_t Normalize(int value) {
    const auto language = static_cast<uint16_t>(value);
    switch (language & 0x3ff) {
    case 0x09: // English
    case 0x04: // Chinese
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
