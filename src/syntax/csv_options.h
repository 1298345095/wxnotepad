#pragma once
#include <cstdint>

class EditorControl;
namespace CsvOption {
// Pinned Styles.cpp / LexCSV.cxx binary lexer.lang fields.
inline constexpr uint32_t Default = ('"' << 8U) | ',';
inline constexpr uint32_t BackslashEscape = 1U << 15U;
inline constexpr uint32_t MergeDelimiter = 1U << 16U;
} // namespace CsvOption
uint32_t SniffCsvOptions(EditorControl& editor, uint32_t previous, bool tabSeparated);
