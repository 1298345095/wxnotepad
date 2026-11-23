# Original Batch, PowerShell and AutoHotkey schemes

The three editing schemes compile the unmodified `LexBatch.cxx`,
`LexPowerShell.cxx` and `LexAutoHotkey.cxx` from Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. They add no interpreter or
language execution dependency. The existing shared importer reads their complete
keyword groups, attributes, styles, extensions and scalar editing metadata.
Its aliases are `Batch → Batch`, `PowerShell → PS1` and `AutoHotkey → AHK`.

The catalog uses the original titles/settings sections `Batch File`,
`PowerShell Script` and `AutoHotkey Script`, with original stable identities
36, 38 and 95. New internal enum members append after existing schemes.
Extension detection retains the existing filename, template and sniffing
precedence. Native Scheme/F12, favorites, defaults, status and Open/Save filters
consume the same catalog entries. The shared style-profile generator discovers
their sections through the importer; their original grouped style identities
and unresolved default strings are available to Default/Dark style application.

The completion engine reuses the original Batch/PowerShell word-character
cache, PowerShell raw-string masks and variable completion source, and
AutoHotkey directive/compiler-directive branches. Comment commands retain the
original `@rem `, `#` and `;` line delimiters. PowerShell and AutoHotkey support
`<#…#>` and `/*…*/` block comments; Batch's native block-comment menu remains
disabled. All three inherit global tab settings with independent 4/4 defaults.

`tests/data/script_languages_reference.tsv` freezes every UTF-8 style byte and
full fold level in six complete/incomplete original documents. It covers Batch
labels, variables, operators and caret continuations; PowerShell here strings,
variables, functions and block comments; and AutoHotkey directives, hotkeys,
hotstrings, continuation sections and block comments. Every document has base,
Delete, Undo and Redo states. The native fixture replays actual incremental
edits against the independent full-restyle expectations.

`script_operations_reference.tsv` contains 47 independently observed native
completion, pairing/quote, newline/indentation, line/block comment, read-only
and rectangle scenarios. It records exact bytes and caret/anchor for the edit,
confirmation, single Undo and Redo, candidate order, native command availability
and rectangle feedback. The original launches a fresh editor per operation.
The native fixture follows the established `autoindent.cpp` setup by clearing
selection-history snapshots between independent scene documents, retaining the
original enabled history option and every recorded assertion.

The original executable SHA-256 is
`c4ae363348fe833b39c9d9b77f1f0b85565f140f7c5e52c51a0b91cf896227d9`.
The owned private-desktop capture, provenance and red/green logs are retained
under `F:/wxnotepad-evidence/style-workflows-implementation-2026-11-23/267/`;
the final original oracle is `original-06`. The capture sends target messages
without global input or clipboard text transfer. Original menu initialization
performs its ordinary clipboard-format availability probe.

Regenerate the attributed tables and shared style metadata offline:

```text
python -X utf8 tools/update_schemes.py --source <original-checkout> --schemes Batch PowerShell AutoHotkey
python -X utf8 tools/style_profiles.py --source <original-checkout>
python -X utf8 tests/script_scheme_import_cli.py --source <original-checkout>
```

Regenerate independent original observations on Windows:

```text
python -X utf8 tests/reference_capture/capture_script_languages.py --exe <owned-original-Notepad4.exe> --output <fresh-evidence-directory>
```

`syntax_script_languages` drives the actual native frame/editor, F12 tree,
editing commands and file dialogs. The packaged process fixture checks English,
Simplified Chinese and Traditional Chinese captions, original section/default
identities, and an independent Restart before saving exact BOM/EOL bytes.
CTest registers Windows GDI/DirectWrite with normal/complementary 96 DPI,
and Linux normal/scaled cases for the existing X11/Wayland wrapper.
