#pragma once

#include <cstdint>

// Application-owned slots from Notepad4; core-owned slots use ScintillaTypes.h.
namespace EditorMargin {
inline constexpr uintptr_t LineNumber = 0;
inline constexpr uintptr_t Bookmark = 1;
inline constexpr uintptr_t Folding = 2;
} // namespace EditorMargin

namespace EditorMarker {
inline constexpr uintptr_t Bookmark = 0;
inline constexpr intptr_t BookmarkMask = intptr_t{1} << Bookmark;
} // namespace EditorMarker

namespace EditorIndicator {
inline constexpr uintptr_t Occurrence = 8;
inline constexpr uintptr_t BraceMatch = 9;
inline constexpr uintptr_t BraceError = 10;
} // namespace EditorIndicator
