# Python highlighting (#34)

The original LexPython module at Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a` owns styling. The generated
`src/python_scheme_data.h` retains keyword groups 0–6 and their PreSorted
attributes from `src/EditLexers/stlPython.cpp`; groups 7–12 are NoLexer
completion data and are omitted. All grouped default light style IDs are
expanded while native font family, point size and DPI remain inherited.
The existing checked source archive supplies the lexer for online and offline
builds, with BSD attribution in `licenses/Notepad4.txt`.

`python_highlighting` drives real Open, language-menu, typing, Delete, Undo and
Redo operations and reads public core styles at known UTF-8 positions. It checks
case-insensitive .py/.pyw/.pyi selection, manual and automatic Save As behavior,
all seven consumed keyword groups, Unicode comments/strings, triple-quoted and
formatted-string edits, grouped palette/font attributes, and independent saved
BOM/mixed-EOL bytes. The lifecycle regressions remain in
`manual_language_choice` and `automatic_language_save`.

The first Windows test failed with expected Python keyword style 6 and actual
Plain Text style 0. The registered original lexer and tables made it pass.
Evidence and platform/static-analysis logs are retained outside the worktree at
`E:/NewNotepad4/wxnotepad-evidence/syntax-phase3/issue-34`.
Python suite folding is tracked separately by #37.
