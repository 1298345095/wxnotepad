#pragma once

#include <cstddef>

namespace SearchLimits {
// Original MRUList and SaveFindReplaceText; history text must be shorter.
inline constexpr std::size_t HistoryEntries = 32;
inline constexpr std::size_t HistoryTextUtf16Units = 2048;
// Original CopySelectionAsFindText uses the core's GetSelText length, including NUL.
inline constexpr std::size_t SelectionBytes = 1024 * 1024;
} // namespace SearchLimits
