#pragma once

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace Ascii {
constexpr int HexDigit(int ch) noexcept {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

constexpr int Lower(int ch) noexcept {
    return ch >= 'A' && ch <= 'Z' ? ch + 'a' - 'A' : ch;
}
} // namespace Ascii
