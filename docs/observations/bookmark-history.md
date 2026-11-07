# Bookmark margin and change history

Reference: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.
The original native commands are `IDM_VIEW_MARGIN` (40413, Ctrl+Shift+M) and
`IDM_VIEW_CHANGE_HISTORY_MARKER` (40422). `Notepad4.cpp` controls both preferences
and the shared margin width; `Styles.cpp::Style_SetBookmark` controls marker zero.

The margin layout is line numbers 0, bookmarks/history 1, folding 2. The shared
bookmark/history margin is native `TextHeight(0) - 2` when either preference is
nonzero and zero otherwise. Font, zoom and DPI changes remeasure the same native
text height. Line numbers and folding retain independent widths. Margin clicks
toggle the clicked logical line only while Bookmark Margin is enabled, keeping
the caret in place. A history-only margin retains background-style bookmarks.

Marker zero remains shared by manual bookmarks and Find All. Visible bookmarks
use `VerticalBookmark`, RGB(64,128,64), opaque; hidden bookmarks use the original
RGB(0,255,0), alpha 40, over-text background. Original change-history markers
21–24 retain the core's symbols and positions. Saved is RGB(0,128,0), modified is
RGB(0,128,255); reverted markers use the line-number background for their interior.

The history toggle follows the original condition `options != 0 || !CanUndo()`.
It never clears history to permit enabling. A pending redo without an available
undo permits enabling, including the original read-only `CanUndo` behavior.
Replacing a document disables tracking before text replacement, then restores
the chosen options after emptying the replacement's undo buffer and establishing
its save point. Ordinary Save establishes the real save point and retains undo.

`tests/bookmark_history_reference.tsv` contains native original observations for
insert/delete, save points, Undo/Redo, the blocked enable transition, redo-only
enable, read-only insertion and toggling, and New. `tests/bookmark_history_files.tsv`
contains independently read UTF-16LE BOM bytes and marker positions from original
Open, native Save, Undo/Redo, native Reload and New. The test replays those
observations through the editor's public commands and application file operations;
Open/Save/New, Undo/Redo, margin menus/shortcut and margin clicks use native paths.
Same-path application Open exercises the existing reload operation.

`bookmark_history_process.py` checks numeric import, seed discovery, disabled
exit saving, explicit save and restore in independent application processes.
The native test samples actual widget paint pixels for vertical bookmarks,
history-only background bookmarks, modified and saved history bars, and remeasures
geometry across zoom and line-number changes. The Windows DPI96 variant uses
the existing complementary thread-awareness emulation without changing monitor
scale.

Raw original capture scripts, states, numeric paint probes and red/green logs
are preserved in
`E:/NewNotepad4/wxnotepad-evidence/editor-navigation-controls-2026-11-07/153/`.
Default DirectWrite captures establish native states and widths. Original numeric
paint samples explicitly select the native GDI command (40046) and use
`PrintWindow` into a temporary DC; only color counts are retained. Windows tests
run on never-activated private desktops. Cross-backend regression and final
integration checks are coordinated at the parent integration branch.
