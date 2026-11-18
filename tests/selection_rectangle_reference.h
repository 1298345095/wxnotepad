// Captured original rectangle input ranges; see --verify-rectangles and selection-commands.md.
#pragma once
#include "selection_commands_reference.h"
namespace SelectionReference {
struct RectangleInput {
    int document, mode;
    long anchor, caret;
    int selections, main;
    std::array<Range, 2> ranges;
};
inline constexpr std::array<RectangleInput, 12> rectangleInputs = {{
    {0, 1, 4, 13, 1, 0, {{{6, 15, 0, 0}, {0, 0, 0, 0}}}},
    {0, 3, 4, 13, 1, 0, {{{6, 15, 0, 0}, {0, 0, 0, 0}}}},
    {0, 1, 13, 4, 1, 0, {{{15, 5, 1, 0}, {0, 0, 0, 0}}}},
    {0, 3, 13, 4, 1, 0, {{{15, 5, 1, 0}, {0, 0, 0, 0}}}},
    {1, 1, 4, 13, 2, 1, {{{6, 3, 0, 0}, {16, 14, 0, 0}}}},
    {1, 3, 4, 13, 2, 1, {{{6, 3, 0, 0}, {16, 14, 0, 0}}}},
    {1, 1, 13, 4, 2, 1, {{{14, 15, 0, 0}, {4, 5, 0, 0}}}},
    {1, 3, 13, 4, 2, 1, {{{14, 15, 0, 0}, {4, 5, 0, 0}}}},
    {1, 1, 34, 120, 1, 0, {{{38, 121, 0, 0}, {0, 0, 0, 0}}}},
    {1, 3, 34, 120, 1, 0, {{{38, 121, 0, 0}, {0, 0, 0, 0}}}},
    {1, 1, 120, 34, 1, 0, {{{121, 38, 0, 0}, {0, 0, 0, 0}}}},
    {1, 3, 120, 34, 1, 0, {{{121, 38, 0, 0}, {0, 0, 0, 0}}}},
}};
} // namespace SelectionReference
