# Original Ruby, Perl and Tcl schemes

These three schemes import the full original tables from Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a` with the existing shared importer:

```sh
python tools/update_schemes.py --source <original-checkout> --schemes Ruby Perl Tcl
python tools/style_profiles.py --source <original-checkout>
```

The original custom `LexRuby.cxx`, `LexPerl.cxx` and `LexTCL.cxx` are compiled
into the maintained original core. No language interpreter is required. Generated
headers retain source hashes and the existing BSD license notice. The importer
accepts the original `NP2STYLEX_SymbolArray` spelling and ignores commented-out
style declarations, preserving Ruby's Symbol Array customization identity without
activating unused Perl styles.

Stable original settings identities are Ruby Script (33), Perl Script (31) and
Tcl Script (35). Each retains global tab inheritance and its independent 4/4
default. Native Scheme/F12/favorites, status text and file filters consume the
shared catalog. Original Ruby filename exceptions, interpreter prefixes, CGI
sniffing before associations and configured favorite extension priorities use
the existing detection path. No new content guesses are added.

Completion retains full original keyword attributes, Ruby/Perl variable groups,
word-character and raw/plain-text masks. All three retain `#` line comments and
unsupported block comments. Ruby's existing keyword/fold indentation path is
shared with Makefile rather than introducing another typing controller.
Default and Dark themes use the original style keys and code-font inheritance.

`tests/data/dynamic_languages_reference.tsv` freezes every UTF-8 byte's style,
every line state and full fold level for six complete/incomplete original
fixtures and their Delete/Undo/Redo states. `dynamic_operations_reference.tsv`
freezes 44 original manual/automatic completion, quote/bracket insertion,
autoindent and protected comment operations, including caret/anchor and single
Undo/Redo. The capture runs a separate owned original executable on a private
Windows desktop:

```sh
python -X utf8 tests/reference_capture/capture_dynamic_languages.py   --exe <pinned-original-Notepad4.exe> --output <fresh-external-evidence-directory>
```

The native fixture drives real menus, F12, native Open/Save As, folding and
Tab Settings; checks exact independently read file bytes, source-encoding reload/reopen,
first Save, Save As, scheme switches with history, file-variable precedence and
theme/font state. Packaged processes check English, Simplified Chinese and
Traditional Chinese identities, styles, clean history, settings, closed-file relaunch and independent Restart. Existing native
customization, menu hierarchy, localized chooser and filter regressions also
cover the three new schemes.

Independent original provenance and red/green/platform evidence are retained in
`F:/wxnotepad-evidence/launch-format-implementation-2026-11-26/281/`.

The high-DPI Windows viewport can hide the end of the complete Ruby fixture.
The native fixture scrolls that final byte into view for a real incremental
paint and restores the viewport; the full original byte/style/line-state/fold
assertions and existing paint/case deadlines remain unchanged.
