# Split Lines and Column Wrap

Compatibility reference: Notepad4 revision `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`Notepad4.cpp` command routing and selection-only menu availability,
`Dialogs.cpp::ColumnWrapDlgProc`, `Edit.cpp::EditWrapToColumn`, and
`scintilla/src/Editor.cxx::LinesSplit`. See [#121](https://github.com/1298345095/wxnotepad/issues/121).

Split Lines forwards `TargetFromSelection` and `LinesSplit(0)` to the pinned core.
Its width is the rendered text viewport. The core expands the main target to
logical lines, including a line beginning at the selected end, and preserves its
own empty-target, rectangular, multiple-selection, measurement and undo behavior.
The native menu and exact Ctrl+I route require a nonempty selection.

Column Wrap adapts the original UTF-16 loop, using the pinned Unicode conversion
helpers. It expands the selected start to the beginning of its logical line and
leaves the selected end unchanged. Existing line separators remain unchanged;
inserted separators use the current EOL mode. Words can exceed the boundary,
spaces and tabs normalize according to the original loop, and a replacement
occurs only after collapsing a whitespace run or inserting a break. A single tab
alone therefore produces no replacement. Replacement repairs one selection in
its original direction, including the original read-only endpoint behavior.

The native dialog starts at the effective `LongLinesLimit` (default 80, imported
and bounded to 0..4096, with the original `fill-column` file-variable override).
Accepted boundaries are clamped to 1..512. The accepted value is recalled until
New or a successful Open replaces the document. Cancel and invalid input preserve
the recalled value. The input retains the original 15-character limit and
unsigned decimal conversion behavior, including leading spaces, accepted Unicode
digits and the signed
assignment of values above INT_MAX. Windows `GetDlgItemInt` was independently
queried for these conversion cases. `tools/capture_column_wrap_digits.py` captures
only single Unicode digits actually accepted by that API; its checked-in ranges
retain fullwidth, Arabic-Indic, superscript and supplementary digit input without
accepting unrelated numeric characters such as Roman numerals or fractions.

`original_line_wrap` drives actual menu/dialog controls, native Ctrl+I and dialog
Return/Escape/Tab keys in a separate GUI process. Literal fixtures cover UTF-16
supplementary characters, tabs and spaces, existing and generated CRLF/LF/CR,
partial/reversed/rectangle/multiple selections, unchanged results, read-only
protection, pending Redo, viewport changes and zoom. Application Open/Save tests
independently read and reopen literal UTF-8 BOM, GBK and UTF-16LE BOM bytes,
including saved Undo and Redo. X11 and Wayland use private displays through the
repository CI test runner; Windows uses the same native test executable.
