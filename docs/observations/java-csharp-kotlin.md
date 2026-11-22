# Original Java, C# and Kotlin schemes

The three schemes compile the unmodified Notepad4 lexer files from revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. The existing
`tools/update_schemes.py` importer generates their complete keyword groups,
keyword attributes, styles, extension lists, editing defaults and settings
section names. Regenerate them offline with:

```text
python -X utf8 tools/update_schemes.py --source E:/NewNotepad4/notepad4 --schemes Java CSharp Kotlin
python -X utf8 tests/java_scheme_import_cli.py --source E:/NewNotepad4/notepad4
```

The generated headers retain source hashes and the original BSD notice. The
existing completion and insertion engines receive the original
`EditAutoC.cpp::InitAutoCompletionCache` masks and original special completion
branches for annotations, documentation tags and C# preprocessor directives.
The lexer, completion collector and file lifecycle algorithms are shared with
the existing schemes.

`tests/data/java_languages_reference.tsv` is an independent original runtime
oracle, separate from the scheme generator and application under test.
`tests/reference_capture/capture_java_languages.py` captured the untouched
original executable through public editor messages and native commands on a
private Windows desktop, using a fresh portable settings directory and no
clipboard input. The original executable SHA-256 is
`c4ae363348fe833b39c9d9b77f1f0b85565f140f7c5e52c51a0b91cf896227d9`.
The capture's process logs and provenance are retained outside Git under
`F:/wxnotepad-evidence/original-controls-implementation-2026-11-22/255/`, under
`original-01` and `original-02`. The final style oracle uses `original-02`;
the original completion/typing rows were identical in both captures.
`original-doc-03` adds three positive interior-comment completions, including
Java/Kotlin documentation tags and C# XML documentation. It retains the
original caret inside the comment; the earlier EOF cases remain as negatives.

The six documents cover Java annotations, generics, templates and text blocks;
C# directives, XML documentation, verbatim, interpolated and raw strings;
Kotlin templates, nested comments, multiline strings and scripts. Each has
complete or malformed/incomplete input and four observed states: baseline,
single-byte deletion of a text-block quote or nested-comment delimiter, Undo
and Redo. These edits change lexical state across subsequent lines. The native application test compares every
text byte, style byte and fold level in all 24 states. The separate operation
oracle covers 23 manual/automatic/documentation completion, confirmation/history and typed
brace/quote scenarios.

The new menu/filter captions retain the original `Java Source`, `C# Source`
and `Kotlin Source` titles. The shared metadata generator reads their stable
original scheme identities (2, 3 and 68) from pinned `EditLexer.h`; they do not
depend on the port's enum positions.

The native workflow fixture also covers original secondary extensions, native
Open/Save As filters, manual and Automatic selection, New, reload, original
comment delimiters, font inheritance, per-scheme indentation/settings, live
selection/history, and UTF-8 BOM, UTF-16LE BOM and GBK disk bytes. The packaged
process fixture runs English, Simplified Chinese and Traditional Chinese,
checks original settings-section import, and actually restarts each language
in an independent application process before saving and restoring Automatic.
