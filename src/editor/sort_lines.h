#pragma once

// Original EditSortFlag values from Notepad4 89b8b36.
namespace SortOption {
inline constexpr unsigned Ascending = 0;
inline constexpr unsigned Descending = 1;
inline constexpr unsigned IgnoreCase = 2;
inline constexpr unsigned LogicalNumber = 4;
inline constexpr unsigned Column = 8;
inline constexpr unsigned FileType = 16;
inline constexpr unsigned DontSort = 32;
inline constexpr unsigned Shuffle = 64;
inline constexpr unsigned MergeDuplicate = 128;
inline constexpr unsigned RemoveDuplicate = 256;
inline constexpr unsigned RemoveUnique = 512;
} // namespace SortOption
