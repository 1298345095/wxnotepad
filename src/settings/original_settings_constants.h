#pragma once

// Notepad4 89b8b36, Edit.h. Keep the persisted option bits unchanged.
enum SelectOption {
    SelectOption_None = 0,
    SelectOption_EnableMultipleSelection = 1,
    SelectOption_CopySelectionAsFindText = 2,
    SelectOption_CopyPasteBufferAsFindText = 4,
    SelectOption_UndoRedoRememberSelection = 8,
    SelectOption_Default = 15,
};

enum AutoInsertMask {
    AutoInsertMask_SpaceAfterComment = 256,
    AutoInsertMask_Default = 511,
};

namespace OriginalSettingLimits {
inline constexpr int MaxTabWidth = 256;
inline constexpr int MaxLongLineColumn = 4096;
} // namespace OriginalSettingLimits
