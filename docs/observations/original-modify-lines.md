# Original Modify Lines

Baseline: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`src/Edit.cpp::EditModifyLines`, `EditModifyLinesText`,
`EditModifyLinesDlgProc`, `src/Notepad4.rc::IDD_MODIFYLINES`, and
`src/Helpers.cpp::MultilineEditProc`.

`EditorControl::ModifyLines(prefix, suffix, skipEmpty)` adapts the original
algorithm against the live core. Its false result requests the original
rectangular-selection feedback. Caret-only scope is the current logical line;
stream selections include the partial first line and exclude a final endpoint
at a later line's start. The original core retains read-only insertion
protection and history.

Each field expands its first recognized `$(L)`, `$(N)`, `$(I)`, `$(0L)`,
`$(0N)` or `$(0I)` token, leaving later tokens literal. Ordinary tokens pad with
spaces. Width uses the complete original participating line range, including
skipped empties. L uses physical line numbers, while independent N/I counters
advance only when their field is inserted. Whitespace-only lines participate.

The native dialog reconstructs WinEdit CRLF input. The operation converts CRLF
pairs to the document EOL and preserves lone CR/LF. Destination-line accounting
skips newly inserted prefix/suffix lines; final selection repair intentionally
uses the original participating line indices. The six native links replace
the whole last-focused field, restore its focus and stay outside Tab traversal.
Main Return and keypad Enter without Shift, including their Ctrl variants, invoke
OK from either field. Shift with either key inserts a line break. OK captures
recalled values and applies while the dialog is still open; Escape and Cancel
preserve accepted recall and document history.

`tests/modify_lines.cpp` verifies public text, exact selection endpoints,
undo/redo/save points, native Alt+M modifiers, main/keypad Return acceptance with
and without Ctrl, Shift with either key for multiline entry in both fields, native
Cancel, all six links, Tab/focus behavior, accepted recall, empty/read-only/rectangular paths,
and independently reopened UTF-8, GBK and UTF-16LE saved bytes. The same native
executable runs on Windows and private X11/Wayland displays. Linux native
shortcut requests use `tests/native_key_helpers.h`, the existing isolated
display transport and steady-clock deadlines with actual key-release checks.

The links disable keyboard focus before native creation: wxMSW uses that policy
when choosing `WS_TABSTOP`. The Windows Tab test enters native dialog-message
processing; accelerator translation handles the shortcut tests. Its focus
assertion runs after closing the dialog, allowing a regression to report failure
through the application test boundary.

Development evidence is retained under
`E:/NewNotepad4/wxnotepad-evidence/everyday-line-editing-2026-11-06/issue-116/`.
Return compatibility and shared-shortcut follow-up evidence is retained beside it
under `review-fixes/`, including Windows/X11/Wayland red and green runs.
