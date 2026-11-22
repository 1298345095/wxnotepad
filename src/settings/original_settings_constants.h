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
    AutoInsertMask_Parenthesis = 1,
    AutoInsertMask_Brace = 2,
    AutoInsertMask_SquareBracket = 4,
    AutoInsertMask_AngleBracket = 8,
    AutoInsertMask_DoubleQuote = 16,
    AutoInsertMask_SingleQuote = 32,
    AutoInsertMask_Backtick = 64,
    AutoInsertMask_SpaceAfterComma = 128,
    AutoInsertMask_SpaceAfterComment = 256,
    AutoInsertMask_Default = 511,
};

enum MarkOccurrenceOption {
    MarkOccurrenceOption_Enabled = 1,
    MarkOccurrenceOption_MatchCase = 2,
    MarkOccurrenceOption_WholeWord = 4,
    MarkOccurrenceOption_Bookmark = 8,
    MarkOccurrenceOption_Default = MarkOccurrenceOption_Enabled,
};

enum class EscapeFunction { None, Minimize, Exit };

enum FullScreenMode {
    FullScreenMode_OnStartup = 1,
    FullScreenMode_HideTitle = 2,
    FullScreenMode_Default = FullScreenMode_HideTitle,
};

// Original Edit.h continuation-indent indices, also used by the native dialog.
enum class WrapContinuationIndent { None, OneCharacter, TwoCharacters, OneLevel, TwoLevels, Same, Indent, DeepIndent };

namespace OriginalSettingLimits {
inline constexpr int MinRecentFiles = 32;
inline constexpr int MaxTabWidth = 256;
inline constexpr int MaxLongLineColumn = 4096;
} // namespace OriginalSettingLimits
