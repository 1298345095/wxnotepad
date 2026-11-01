# Original Replace in Selection

Compatibility source: Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, `src/Edit.cpp`,
`EditReplaceAllInSelection` (5864–5941), `UpdateFindReplaceDlgItem` (4699–4705),
and `src/Notepad4.rc` (`IDC_REPLACEINSEL`, `IDS_SELRECT`).

The Replace dialog exposes **In Selection** when find text is nonempty, including
an empty stream selection or a rectangular selection. Find view hides it. A
rectangular selection produces the original warning before pattern preparation.

The operation searches forward from the original selection start to document end
using the shared original matcher, options and capture formatter. It accepts a
match only if its end is at or before the live selection end, and stops at the
first crossing match. Thus assertions retain document context, consuming matches
cannot extend outside the selected range, and an empty match at the selection
end can insert text there. Growth/shrinkage follows core byte-position updates.
The original character/pattern-leading-caret progress rules remain shared with
Replace All.

Selection endpoints follow the original core: insertion at the beginning of a
still-nonempty selection moves that endpoint past inserted bytes. After replacing
an entire selection or inserting at its end, final repair extends the correct
endpoint to the last replacement end. Direction is retained when nonempty.
Text-changing replacements form one Undo action; byte-identical replacements
count without clearing Redo or the save point.

`replace_in_selection` exercises actual editor commands and native dialog controls,
including the unsupported-selection warning. It covers literal/regex scope,
cross-boundary alternatives, multiline/Unicode ranges, both directions, empty
matches/ranges, growth/shrinkage, counts, invalid/no-match/no-op outcomes and
Undo/Redo. Independently read saved UTF-8 BOM/mixed-EOL bytes verify that both
growth and shrinkage preserve text outside the selected range.

Execution records are retained outside the repository at
`E:/NewNotepad4/wxnotepad-evidence/original-parity-implementation/issue-77/`.
