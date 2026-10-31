# C/C++ syntax validation (#31)

The editor uses the pinned, unmodified Notepad4 `LexCPP.cxx`, 13 lexer-consumed
keyword lists and the original default light style groups. The generated data
and offline regeneration path are documented in `patches/README.md`. C/C++ does
not import upstream encoding, newline, indentation or font defaults.

`tests/syntax_cpp.cpp` opens real files through `EditorFrame::Open`, invokes the
actual New/Undo/Redo menu commands and sends owned character/Delete events. It
checks the core's public style IDs at literal UTF-8 positions (including Chinese
comments and strings), grouped foreground/background/bold attributes, native
font inheritance, all eight ASCII case-insensitive extensions, cancellation and
failure retention, Plain Text reset and independently read saved BOM/EOL bytes.
Multiline-comment edits must restyle following lines across Undo/Redo while
retaining caret, history and save point. A long document also checks that an edit
leaves work for the core's incremental styling lifecycle.

Final targeted regression: **9/9 Windows, 9/9 private X11, 9/9 private Wayland**.
The group includes the new syntax test and existing Open, save, EOL, UTF-8,
UTF-16LE, GBK, input/Undo and native-font checks. The new test waits for visible
styling with a bounded native event pump on asynchronous Wayland frame callbacks;
it never forces `Colourise`.

The initial Windows test failed before implementation because `.CPP` still used
Plain Text. Red/green logs, intermediate Wayland test-timing failures, final
backend logs, original source archive, final source snapshot and targeted
clang-tidy logs are preserved externally at
`E:/NewNotepad4/wxnotepad-evidence/syntax-phase3/issue-31`.

Canonical clang-format 23.1.2 passes. Targeted clang-tidy checks cover the new
implementation and behavioral test on Windows and Linux. Remaining diagnostics
are the repository's `#pragma once` convention on Windows and existing document
enum-size suggestions; there are no new functional diagnostics. Broad integrated
regression and static-analysis scans are managed by the phase integration task.
