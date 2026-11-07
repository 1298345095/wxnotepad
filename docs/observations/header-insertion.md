# Encoding identifier and script shebang insertion

Reference: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, issue #156.
`Notepad4.cpp` 3435–3460 supplies encoding-name and Python-header rules;
`EditAutoC.cpp` 2840–2941 supplies interpreter and EOL rules.

The shared insertion commands consume language metadata from `SyntaxDefinition`.
Python supplies its coding-header capability and the `python3` interpreter. Plain
Text, C/C++ and JSON use the original generic `#!/usr/bin/env ` prefix. Encoding
names come from the first original encoding parse name; UTF-16LE with BOM uses
the original short status-name fallback, `UTF-16LE BOM`.

Python inserts `#-*- coding: NAME -*-` when the current caret is at column zero
in either of the first two document lines. All other positions insert the name.
Selection direction determines the current caret used by this rule. A Python
shebang adds the document's CRLF, CR or LF only when the current caret is zero.
The generic shebang prefix adds no EOL. Both commands use the core's original
`ReplaceSel` behavior for stream, rectangular and protected selections.

Protected `ReplaceSel` retains text but collapses a nonempty selection and
clears an existing redo branch. The implementation deliberately retains this
observed behavior.

## Independent original fixtures

`tools/capture_header_insert.py` captures the original process through owned
Windows menu/Scintilla handles, launched using the existing issue93 private
desktop runner. It uses no clipboard, physical desktop input or screenshots.
Run its normal mode, `--extra` (explicit GBK and UTF-16LE without BOM), and
`--redo` (protected selections with existing redo).

The raw captures and saved files are preserved under
`E:/NewNotepad4/wxnotepad-evidence/editor-navigation-controls-2026-11-07/156`:
`header-reference.tsv`, `header-extra-reference.tsv`, `header-redo-reference.tsv`,
and the corresponding `original*.json` files. `tests/header_reference.tsv`
concatenates their records after the first header. `tests/header_saved.tsv`
contains their independently read saved bytes for all 48 language/encoding/command
combinations.

The 1,248 native GUI scenarios inspect text, caret/anchor, modified state and
Undo/Redo at all three stages. They cover four delivered languages, three EOL
modes, encoding identities, directed selections, rectangles and protection.
Native Ctrl+F8 tests additionally check exact modifiers, neighboring shortcuts,
empty documents, protected replacement and the owned editor receiver. Insertion
retains encoding, BOM, EOL, font size and zoom. The CP936 loss-cancellation case
uses the existing document save decision and independently reads unchanged disk
bytes.

`tests/header_insert_process.py` starts 48 independent actual application
processes and checks saved, undone, redone and reloaded bytes against the original
captures. Default ANSI, explicit GBK, UTF-8 with/without BOM and UTF-16LE
with/without BOM remain distinct identities; normal encoding-tag interpretation
remains enabled.

## TDD and publication contention

`156/red1.log` records the missing Encoding Identifier native-menu failure;
`156/green1.log` records its first passing insertion/Undo/Redo slice.
`156/red2.log` records the missing Script Shebang Line native-menu failure.
The complete GUI and independent-process checks are recorded in
`156/final-tests.log`.

The independent-process matrix exposed a pre-existing observation snapshot race.
wxWidgets 3.3.3 `wxRenameFile` can copy over an open destination after rename
fails, temporarily exposing empty or partial state. The existing shutdown test
now holds a CRT reader and verifies that the previous complete snapshot remains
unchanged until replacement is possible. `156/snapshot-red.log` records the
deterministic failure. Publication now uses native atomic rename, as document
saving already does, and retains the existing publication retry. Consumers keep
their complete-state assertions.
