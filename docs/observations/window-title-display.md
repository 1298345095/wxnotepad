# Window title display

The reference is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`UpdateWindowTitle`, `EditGetExcerpt`, `FileLoad`, `FileSave`,
`GetRelaunchParameters`, and the English/zh-Hans/zh-Hant resource menus.
This implements [#257](https://github.com/1298345095/wxnotepad/issues/257).

Settings → Window Title Display follows Appearance and precedes the separator
before File Change Notification. Its three path choices retain the original
`PathNameFormat` values: File Name Only = 0, File Name and Directory = 1
(default), Full Path Name = 2. Imported values clamp to this range. The original
mutually exclusive radio marks remain visible; choosing Text Excerpt again
captures the new selection even when that command is already checked.

A captured excerpt replaces the document-name portion of the actual native
title. The existing modified prefix, translated read-only annotation and
wxnotepad identity still apply. File protection takes precedence over editor
protection. Path spelling, roots and separators use the native platform.

## Captured selection

The excerpt reads at most 255 editor bytes. Native ANSI documents use their
native byte representation; other documents use UTF-8. The original Win32 UTF-8
replacement rule handles a character split at the byte boundary. Control
characters through U+0020 become spaces, adjacent spaces collapse, leading
spaces disappear, and a final space is retained. An all-whitespace selection
becomes ` ... `. Empty or rectangular selections clear the excerpt and restore
the selected path mode. Neither capture nor a path-mode command changes the
document, selection or undo/redo history.

The original 128-unit UTF-16 buffer stores 127 units. Exactly 128 normalized
units also show 127 units without ellipsis; only more than 128 units replace the
tail with `...` (124 units plus three dots). The migrated implementation retains
these visible boundaries without the original out-of-bounds terminator.

English quotes are `"…"`, Simplified Chinese quotes are `“…”`, and Traditional
Chinese quotes are `「…」`. These are the original resource strings, independently
observed in the original application with private resource-only locale DLLs.

## Lifetime and preferences

Selection movement, later edits, Undo/Redo, protection changes, ordinary Save
and Save Copy retain the captured value. A path-mode command clears it. New,
successful Save As, saving an untitled document to its first filename, and
opening a different file clear it. Revert and explicit reloads of the current
file retain it.

The native Open chooser also clears an excerpt when choosing the current file.
The original decides retention before the chooser supplies its filename;
explicit known-path loading of that same file retains the excerpt instead.
Cancelling Open or Save As retains the previous capture.

New Window and Restart do not transfer captured excerpts. They retain the
existing startup/settings behavior and document identity. `PathNameFormat`
round-trips through Save Settings Now and Save Settings On Exit, omitting its
default value. Disabled automatic saving retains the stored preference and
unrelated INI contents. No persistent excerpt preference or additional command
line mode is introduced.

## Evidence and regression seams

Independent original captures are retained outside Git under
`F:/wxnotepad-evidence/original-controls-implementation-2026-11-22/257/`:
`original-12`, `original-zh-Hans`, `original-zh-Hant`, and `original-ansi`, with
their `*-probe` command logs. The Unicode/whitespace/byte-limit fixture in
`tests/window_title_reference.tsv` was transcribed from the earlier successful
`original-01/samples.json`. Native ANSI evidence records host ACP 936: 200
Chinese characters produce an excerpt containing 127 Chinese characters.

`original_window_title` activates native menus, reads the actual HWND/GTK title,
checks native radio marks, compares frozen original excerpts, and exercises
document/protection/chooser lifecycles. Its Chinese variants check the actual
translated native menus and titles. `original_window_title_process` observes
shown independent applications, menu checks, INI writes, disabled/explicit/default
saving and New Window/Restart. Windows additionally runs the native ANSI fixture.
