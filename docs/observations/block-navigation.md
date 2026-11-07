# Original block navigation and selection

Issue: [#150](https://github.com/1298345095/wxnotepad/issues/150).
The reference is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`,
`Edit.cpp::EditGotoBlock` and `EditSelectLines`, with the native accelerator
table in `Notepad4.rc`.

The Edit menu exposes block start/end, previous/next block, previous/next
sibling block, and selection of lines in the current block. Their shortcuts
are Alt+[, Alt+], Alt+Comma, Alt+Period, Ctrl+Alt+Comma, Ctrl+Alt+Period,
and Alt+Shift+] respectively. Extra modifiers retain the original routing;
Ctrl+Shift+Comma/Period activate the selection's start/end without changing
the selected text.

Commands traverse the active lexer's fold tree. Navigation retains the
visual column and unfolds and reveals the destination. Block selection uses
the main caret's block, retains backward selections, and directs an empty
selection toward the nearest boundary (ties go forward). Its line-selection
behavior uses the existing Visual Studio mode bit, including Old Visual
Studio. A caret without a fold parent selects the whole document. Plain
Text uses the original null lexer's indentation folding.

`tests/reference_capture/capture_block_navigation.py` captures native original-process
WM_COMMAND results and public Scintilla state without clipboard access:

```text
python -X utf8 tests/reference_capture/capture_block_navigation.py ORIGINAL_EXE EVIDENCE_DIRECTORY tests/block_navigation_fixtures.h
clang-format -i tests/block_navigation_fixtures.h
```

The committed 1,062 fixtures cover every line of small C/C++, Python, JSON,
and Plain Text documents and selected lines of a long Unicode C++ document.
They include nested/sibling headers and bodies, whitespace and final lines
without EOL, document boundaries, all four line-selection preferences,
empty/forward/backward selections, rectangles with virtual space, multiple
selections, read-only documents, and contracted folds. The external JSON
also records before/after bytes, save-point/history availability, UTF-8
codepage, visibility and expansion state. Toggling the original Use Default
Code Style leaves the captured folding trees unchanged.

The registered `original_block_navigation` test replays these expectations
through native menu commands and drives every punctuation shortcut through
the shared native keyboard helper. It checks directed endpoints, selection
mode/count, virtual space, visibility, expansion state, viewport reveal,
and working Undo/Redo. A separate file scenario switches Automatic and
Plain Text on the same document and checks independently read UTF-16LE
file bytes and BOM after saving. The test is registered for the existing
Windows, private X11, and private Wayland runners.

The fixture seeds a pending Redo by clearing the document and undoing that
edit. The pinned core's `Document::Undo` calls `ModifiedAt(0)`, invalidating
styling from the beginning of the restored text. The cleared fold data also
needs rebuilding. Full colourising and native painting therefore happen
after this history setup. The fixture
asserts complete styling and the independently captured first-line fold
header before running its first menu command, so its starting fold tree
does not depend on event-loop repaint timing.
The complete-styling check accepts an end-styled position at or beyond the
document length: the pinned `LexAccessor::StyleEndPos` includes an EOF
sentinel, which release `StyleContext::Complete` can style.
`NativeFold::Paint` uses the shared `NativePaint::Complete` seam: two actual
native paint handlers must return, with a steady-clock deadline for each.
This settles the post-styling layout on wxGTK/Wayland, where `Update()` alone
does not deliver a native paint. An uncompleted paint fails explicitly.
