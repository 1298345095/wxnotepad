# JavaScript and TypeScript schemes (#179)

The pinned Notepad4 revision `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`
supplies `LexJavaScript.cxx`, JavaScript/TypeScript consumed keyword lists,
per-list attributes, grouped default styles and extension sets. The original
BSD license is retained in `licenses/Notepad4.txt`. Run
`python tools/update_javascript_scheme.py --source /path/to/notepad4` to reproduce
the two headers; `--output-dir` supports comparison without changing the checkout.
Completion-only lists 8–11 are omitted; empty consumed indices remain present.

`tests/fixtures/javascript/original.h` records complete byte styles and fold levels
from the full original Windows application on an owned, never-activated desktop.
The four input files cover nested objects/functions, documentation and line
comments, template interpolation, regex/division, JSX tags and TS/TSX types.
The same application deletes the first byte and performs Undo/Redo, verifying
that Undo restores the original snapshot and Redo restores the edited snapshot.
`javascript_typescript_schemes` compares every byte and line against these
independent observations before/after edits and Undo/Redo.

Windows focused validation passes native Scheme choices, complete original
style/fold comparisons, all original extension variants and mixed ASCII case,
node shebang/Automatic selection, first Save and Save As state transitions,
same-path reopen, independent disk bytes, shared comment/fold/search commands,
and original independent JavaScript/TypeScript INI sections through JSX/TSX aliases,
including global/scheme/file indentation precedence. Non-editing scheme switches
preserve native point size, zoom and redo history.
`automatic_language_save` also drives the actual native file chooser for all four
JS/TS types. Generator tests use real temporary Git fixtures and independently
assert grouped palette, attributes, extensions and completion-list omission.
The generator has 97% focused coverage with branch measurement enabled, above
the unchanged 80% gate.

Raw capture, JSON snapshots, red/green build/test logs and JUnit results are in
`E:/NewNotepad4/wxnotepad-evidence/encoding-languages-2026-11-08/issue-179`.
Complete Windows and isolated X11/Wayland regression and native analysis are
performed on the integration branch; final results belong to the overview's
validation evidence.
Microsoft Pinyin remains covered by the existing human validation; Linux IME
validation remains deferred.
