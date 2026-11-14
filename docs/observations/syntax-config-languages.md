# Original TOML, YAML and CMake schemes

The three schemes reuse the shared importer described in
[the native-language handoff](syntax-native-languages-README.md#shared-import-path).
`tools/update_schemes.py --source <original-checkout> --schemes TOML YAML CMake`
imports the complete pinned `stlTOML.cpp`, `stlYAML.cpp` and `stlCMake.cpp` tables.
Their generated headers retain source hashes, original keyword attribute bytes,
null completion-only groups and scalar editing metadata. The maintained original
core compiles `LexTOML.cxx`, `LexYAML.cxx` and `LexCMake.cxx` from Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a` under its existing BSD license.

TOML and YAML retain original independent 2/2 spaces and original settings
sections `TOML File` and `YAML Document`. CMake uses `CMake Script`, with original
4/4 defaults and global inheritance. YAML selects forward-looking indentation
guides. Comment delimiters and native folding/navigation use the existing shared
commands. TOML table and YAML document styles retain bold, background and
end-of-line fill through the existing style representation.

The original CMake `.txt` filename exceptions precede normal extension matching:
`CMakeLists.txt` and `CMakeCache.txt`, case-insensitively. All original extensions,
YAML dotfile configuration names and the shared one-suffix `.in` template rule
remain data-driven. Scheme selection, manual overrides, reopen and Save As use
the existing document lifecycle.

Pinned `EditAutoC.cpp::InitAutoCompletionCache` has no dedicated TOML, YAML or
CMake cases. Their raw-string, generic-type and character-prefix masks therefore
remain empty; comment/string masks come from the original scalar metadata.
`completion_styles.cpp` records that deliberate absence explicitly. In
particular, YAML's zero string range is retained instead of inventing masks from
its display palette. The native typed-character oracle covers those contexts.

`tests/data/config_languages_reference.tsv` freezes every UTF-8 byte's style and
full fold levels for base, Delete, Undo and Redo in the pinned original native
executable. It covers TOML tables/keys, multiline basic/literal strings, escapes,
numbers and dates; YAML directives, anchors, aliases, quotes and block scalars;
and CMake commands, variables, generator expressions, bracket arguments/comments
and function/conditional folds. `config_operations_reference.tsv` adds 21 original
block-navigation commands and 12 typed-character cases, including each original
Undo/Redo step. Expected results are independent of the importer and port.

`tests/syntax_config_languages.cpp` drives actual native editor and application
commands. Its workflow matrix also checks menu selection, manual override/reopen,
highlight switches with pending history, folding, comments, original indentation
and native Tab Settings persistence, first Save, Save As, native Open/Save As
dialogs, exact independently read BOM/mixed-EOL bytes, and independently read
UTF-16LE settings section names and tab widths.

Regenerate independent captures on Windows with:

```sh
python -X utf8 tests/reference_capture/capture_config_languages.py \
  --exe <pinned-original-Notepad4.exe> --output <external-evidence-directory>
```

The utility creates an owned private desktop and portable host, without global
clipboard or screenshot inspection. Raw original JSON, red/green logs, builds,
analyzer reports and platform results are retained in
`E:/NewNotepad4/wxnotepad-evidence/completion-languages-2026-11-10/issue-194/`.
Final Spec/Standards reviews and integrated IME checks are coordinated by the
parent implementation task.

Local validation passed nine syntax/save targets on Windows and X11. The eight
existing targets passed on Wayland; the configuration-language target then passed
on all three backends after its full-byte oracle was made to wait for completed
native paints. Its CMake edit is past byte 128, beyond the shared helper's prefix
readiness check. No expected style/fold values changed. All tracked C/C++ files
pass canonical clang-format 23.1.2; affected native translation units pass the
configured clang-tidy checks on Windows (23) and Linux (18). Python discovery
passed 30 tests with three platform skips; both the new and existing shared
import CLI checks passed.

The broad syntax run also exposed a Python fixture interaction with the restored
original pairing default: typing a replacement triple-string quote inserted a
fourth quote. The lexer-only edit scenes now explicitly disable automatic
insertion while preserving all original byte/style/history assertions. The
enabled pairing behavior remains covered by its separate original oracle.
`python-scene-native.log` retains the exact failure text, bytes and style before
that fixture-only correction. Product pairing defaults are unchanged.
