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

`tests/reference_capture/capture_header_insert.py` captures the original process through owned
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

On a supported native-ANSI host, the 1,248 native GUI scenarios inspect text, caret/anchor, modified state and
Undo/Redo at all three stages. They cover four delivered languages, three EOL
modes, encoding identities, directed selections, rectangles and protection.
Native Ctrl+F8 tests additionally check exact modifiers, neighboring shortcuts,
empty documents, protected replacement and the owned editor receiver. Insertion
retains encoding, BOM, EOL, font size and zoom. The CP936 loss-cancellation case
uses the existing document save decision and independently reads unchanged disk
bytes.

`tests/header_insert_process.py` starts 48 independent actual application
processes and checks saved, undone, redone and reloaded bytes against the original
captures. The reference set distinguishes default ANSI, explicit GBK, UTF-8
with/without BOM and UTF-16LE with/without BOM; normal encoding-tag interpretation
remains enabled.

The loaded identity follows the existing native encoding policy. On Linux with a
UTF-8 locale, a configured default-ANSI load becomes explicit UTF-8. Original
`Edit.cpp` 1106–1119 promotes ANSI to `Encoding_GetIndex(legacyACP)`; the Linux
port resolves both native and legacy pages to its UTF-8 locale. The tests establish
that locale independently through wxWidgets' system-locale query and Python's
`locale.getencoding()` (which ignores Python UTF-8 mode), assert UTF-8/no-BOM after
Open, and select the matching independently captured UTF-8 record for every
text/selection/history/saved-byte comparison. The raw ANSI records and Windows
ANSI context remain unchanged. Explicit GBK and the other encoding cases retain
their original records on every backend.

Native ANSI also depends on the migrated codec set documented in
[`docs/encoding.md`](../encoding.md). A Windows native page such as 1252 is
recognized but unmigrated. `DefaultEncoding=0` therefore rejects its automatic
Open before a header command can run. The native test applies each of those 216
records to the public Open refusal boundary and verifies unchanged text,
selection, history, protection, language, EOL, codec, BOM, path and disk bytes.
It continues to apply every insertion assertion for the 1,032 constructible
records. The independent-process test verifies eight such refusals while retaining
a modified UTF-8/BOM document; original captured UTF-8/BOM bytes, Undo/Redo saves
and reload prove the old format, history and save target survive. The other 40
contexts retain their original saved-byte assertions. Both tests print supported
and refused context counts. On native ACP936 all 1,248 insertion records and 48
saved-byte contexts still run, including every raw ANSI value. UTF-8 ACP/legacy
ACP routing remains unchanged.

Hosted Windows validation first exposed this setup assumption. A process-only
`activeCodePage=en-US` manifest on disposable executable copies reproduced the
same Open refusal and startup exit 255 with real ACP1252/OEM437 and the original
`DefaultEncoding=0`. No system code-page change, production hook or codec expansion
was used. Failure diagnostics now identify the language, encoding, operation,
native page and Open error rather than stopping at a generic fixture assertion.

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
