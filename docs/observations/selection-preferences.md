# Selection preference controls (#188)

Settings → Select and Edit Options now exposes Enable Multiple Selection before
the two find-text preferences, and Undo Redo Remember Selection after them.
The checked commands update the existing `SelectOption` bits and live maintained
core. The original defaults, raw unrelated bits and current settings lifecycle
remain authoritative. Frames without a store use their in-memory preferences.

The behavior follows Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, `Notepad4.rc`'s selection submenu,
`Notepad4.cpp`'s selection-option handlers, and the maintained original
`Editor::SetMultipleSelection` / `EditModel::ChangeUndoSelectionHistory` operations.
Turning multiple selection off leaves active additional ranges intact. Turning
selection memory off clears selection snapshots and retains text history/save
state. Turning it back on starts recording new snapshots.

`tests/reference_capture/capture_selection_preferences.py` copies the pinned
original executable into an owned portable host and drives it on a private Win32
desktop. It never uses the clipboard. The raw 23-state capture is retained under
`E:/NewNotepad4/wxnotepad-evidence/completion-languages-2026-11-10/issue-188/`;
the reviewed scalar states are frozen in `tests/selection_preferences_reference.tsv`.
The capture covers active ranges, forward/reverse selection history, both scroll
axes, text Undo/Redo/save state, snapshot clearing, and native line typing.
History fixtures use long lines and visible caret columns so their offsets are
valid across native font and viewport sizes.

The hosted 96-DPI run exposed that column 2 lies left of a 20-pixel horizontal
offset with its smaller font. The pinned original also scrolls back to zero when
Undo restores that reversed caret. The fixture now selects columns 20–24 in a
420-DIP-wide frame, verifies that the text and scroll width exceed the client
width, and checks both endpoints stay inside the scrolled viewport. New original
captures at 96 and 144 DPI agree on all 23 frozen states; exact horizontal and
vertical scroll assertions remain enabled. Windows also registers
`original_selection_preferences_dpi96`, using an unaware test thread without
changing monitor scaling. The fixture waits for completed native editor paints
before measuring scroll width: `Update()` alone does not complete the asynchronous
wxGTK/Wayland paint that updates this value.

The regressions exercise these public boundaries:

- `original_selection_option_gates`: independent INI imports, native menu command
  activation, checked state, live core state and preservation of raw bits, including
  negative values; startup, New, Open and reopen retain the preferences.
- `original_selection_preferences`: a frame without a settings store; frozen
  original states and native Win32/GDK Ctrl/Alt mouse gestures, typing and Delete;
  additional ranges, rectangles and line selection retain original text outcomes.
  Win32 uses complete keydown/character/keyup sequences and the dedicated Delete
  key's extended flag. Motion follows a path because wxMSW filters duplicate
  coordinates before the core's drag throttling.
- `original_selection_preferences_lifecycle`: separate real frame processes and
  independently read INI bytes for enabled exit save, disabled exit save, explicit
  Save Settings Now, reopening, retained unknown fields and unrelated preferences.
  Its `--installed-directory` option also exercises isolated Linux XDG user settings
  from an owned installation prefix.
- `original_menu_hierarchy` and `selection_modifiers`: existing native menu ordering
  and selection behavior regressions remain applicable.

Reference capture and Windows regressions use private desktops; Linux runs use
the existing private X11/Wayland display runner. No screenshot inspection or
shared clipboard modification is involved. Physical desktop pointer/focus and
visual feedback remain human checks; Linux IME remains deferred.
