#pragma once
#include <array>
#include <cstdint>

namespace BraceReference {
// Captured from Notepad4 89b8b36 on Windows; source SCN_UPDATEUI supplies
// the matched (34) / bad (35) overlay choice. See the observation document.
struct Fixture {
    const char* name;
    const char* text;
    intptr_t anchor;
    intptr_t caret;
    intptr_t position;
    std::array<intptr_t, 4> match; // Plain Text, C/C++, Python, JSON.
    intptr_t guide;
};
inline constexpr Fixture fixtures[] = {
    {"before", "  (x)", 2, 2, 2, {4, 4, 4, 4}, 2},
    {"after", "  (x)", 3, 3, 2, {4, 4, 4, 4}, 2},
    {"closing", "  (x)", 4, 4, 4, {2, 2, 2, 2}, 2},
    {"eof", "  (x)", 5, 5, 4, {2, 2, 2, 2}, 2},
    {"precedence", "()[]", 2, 2, 2, {3, 3, 3, 3}, 2},
    {"bad-precedence", "()(", 2, 2, 2, {-1, -1, -1, -1}, 0},
    {"nested", "([{}])", 1, 1, 1, {4, 4, 4, 4}, 1},
    {"angle", "<x>", 0, 0, 0, {2, 2, 2, 2}, 0},
    {"none", "(x) word", 5, 5, -1, {-1, -1, -1, -1}, 0},
    {"empty", "", 0, 0, -1, {-1, -1, -1, -1}, 0},
    {"unicode", "中(a😀b)尾", 3, 3, 3, {10, 10, 10, 10}, 1},
    {"unicode-after", "中(a😀b)尾", 11, 11, 10, {3, 3, 3, 3}, 1},
    {"selection", "  (x)", 0, 2, 2, {4, 4, 4, 4}, 2},
    {"reversed", "  (x)", 5, 2, 2, {4, 4, 4, 4}, 2},
    {"string", "[\"]\"]", 0, 0, 0, {2, 4, 4, 4}, 0},
    {"string-bad", "[\"]\"]", 2, 2, 2, {0, -1, -1, -1}, 0},
    {"comment", "( /* ) */ x)", 0, 0, 0, {5, 11, 5, 11}, 0},
    {"line-comment", "( # )\n)", 0, 0, 0, {4, 4, 6, 4}, 0},
};
} // namespace BraceReference
