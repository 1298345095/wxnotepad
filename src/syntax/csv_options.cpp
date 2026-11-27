#include "syntax/csv_options.h"
#include "editor/editor_control.h"
#include <array>
#include <algorithm>
#include <cstdlib>

uint32_t SniffCsvOptions(EditorControl& editor, uint32_t previous, bool tabSeparated) {
    // Scalar port of pinned Styles.cpp::Style_SniffCSV. Count raw bytes in the
    // first two physical lines, including candidate delimiters inside quotes.
    if (tabSeparated) return ('"' << 8U) | '\t';
    using Scintilla::Message;
    const auto length = editor.Send(Message::PositionFromLine, 2);
    const auto* bytes = reinterpret_cast<const unsigned char*>(editor.Send(Message::GetRangePointer, 0, length));
    if (!bytes) return previous;
    constexpr auto candidates = std::to_array<unsigned char>({';', '|', '\t', ','});
    std::array<std::array<int, candidates.size()>, 2> counts{};
    size_t row = 0;
    for (intptr_t position = 0; position < length; ++position) {
        const auto ch = bytes[position];
        if (ch == '\r' || ch == '\n') {
            row = 1;
            if (ch == '\r' && position + 1 < length && bytes[position + 1] == '\n') ++position;
        } else {
            for (size_t index = 0; index < candidates.size(); ++index)
                if (ch == candidates[index]) ++counts[row][index];
        }
    }
    unsigned char delimiter = 0;
    int maximum = 0;
    constexpr int SimilarityFactor = 4; // original 75% similarity rule
    for (size_t index = 0; index < candidates.size(); ++index) {
        const int first = counts[0][index];
        const int second = counts[1][index];
        const int columns = std::min(first, second);
        if (columns > maximum && columns >= SimilarityFactor * std::abs(first - second)) {
            maximum = columns;
            delimiter = candidates[index];
        }
    }
    if (!delimiter) {
        for (size_t index = 0; index < candidates.size(); ++index) {
            const int columns = counts[0][index] + counts[1][index];
            if (columns > maximum) {
                maximum = columns;
                delimiter = candidates[index];
            }
        }
    }
    return delimiter ? ('"' << 8U) | delimiter : previous;
}
