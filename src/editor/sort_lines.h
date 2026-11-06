#pragma once

// Original EditSortFlag values from Notepad4 89b8b36.
namespace SortOption {
enum : unsigned {
    Ascending = 0,
    Descending = 1,
    IgnoreCase = 2,
    LogicalNumber = 4,
    Column = 8,
    FileType = 16,
    DontSort = 32,
    Shuffle = 64,
    MergeDuplicate = 128,
    RemoveDuplicate = 256,
    RemoveUnique = 512,
};
}
