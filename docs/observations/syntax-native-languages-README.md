# Original Rust, Go and Lua schemes

The catalog uses the dedicated `LexRust.cxx`, `LexGo.cxx` and `LexLua.cxx` from
Notepad4 revision `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. Scheme data comes
from the matching `src/EditLexers/stlRust.cpp`, `stlGo.cpp` and `stlLua.cpp`.
Copyright and the original BSD license are retained in `licenses/Notepad4.txt`.

## Shared import path

Run `python tools/update_schemes.py --source <original-checkout>` to reproduce
all ordinary delivered tables. `--schemes Rust Go Lua --output-dir <directory>`
selects a batch and a reviewable destination. `scheme_data.py` extends the existing
extraction helpers; it reads the fixed original table and generated settings
shapes, preserving the C++ string initializers instead of implementing a C++ parser.

Every keyword group and its original attribute byte survives, including null
slots, `NoLexer` completion groups, and `NoAutoComp` groups. Unknown named flags
fail generation. `SyntaxKeywords::attributes` retains the public
`SCI_SETKEYWORDS` attribute-byte layout. Scheme installation skips `NoLexer`
groups; completion consumers can read the full table. The original scalar editing
metadata and tab defaults are retained in `SyntaxOriginalMetadata`. Original
settings section identities are `Rust Source`, `Go Source` and `Lua Script`.

The earlier C++, Python, JSON, Bash, JavaScript/TypeScript and HTML/XML generators
now use the same extraction path. Historical CLI entry points remain aliases.
HTML/XML embedded style offsets retain their focused adapter. Existing catalog
extension registration is preserved for C++, Python, JSON and Bash; the three new
schemes use every original extension. Original light styles inherit the existing
native font and point size.

For another ordinary scheme, add its file/symbol alias to `update_schemes.py`,
generate its header, register its catalog entry and original lexer, and add a
fixture to the native matrix. The original completion cache also has consumed
style data beyond `EDITLEXER`: extend `completion_styles.cpp` from pinned
`EditAutoC.cpp::InitAutoCompletionCache` for its raw-string, generic-type,
character-prefix, word, comment and string masks. TOML/YAML/CMake and Markdown
should use these shared paths; genuine original format differences belong in
small explicit adapters.

## Independent reference and native matrix

`tests/data/native_languages_reference.tsv` contains 12 states captured from the
pinned original executable: base, one byte deletion, Undo and Redo for each
language. Expected text, every UTF-8 byte's style and every full fold level are
frozen independently of the generator and port. Rust covers lifetimes, characters,
byte characters, raw/raw-byte strings and nested comments. Go covers rune literals,
raw strings and formatting. Lua covers long-bracket strings/comments and builtin
calls. `tests/syntax_native_languages.cpp` reads these states and drives the real
editor's Delete and Undo/Redo commands.

The same fixture matrix exercises original/case-insensitive extensions, manual
selection, same-path reopen, Plain Text suspension/resume, native font sizes,
folding, shared line/block comment commands, inherited and independent indentation,
native Tab Settings persistence, first Save, Save As with explicit type or All
Files, UTF-8 BOM and mixed CRLF/LF bytes. Actual native Open and Save As dialogs
run for all three languages; saved bytes are read separately from disk.

Local validation passed the new `original_rust_go_lua_schemes` test plus seven
existing syntax/save regressions on private Windows, X11 and Wayland: 8/8 per
backend. Canonical clang-format 23.1.2 passed all tracked C/C++ inputs. Native
clang-tidy 23 on Windows and 18 on Linux checked the catalog, scheme installation,
settings consumer and native matrix. Compiler/analyzer checks passed; the new
signed fold-mask warning was corrected. Existing header/API/enum diagnostics are
retained, without a repository-wide warning-free claim. Python discovery passed
30 tests with three platform skips using `python -X utf8`. After merging the
shared paired-editing integration, the complete language matrix was rebuilt and
passed again on Windows, X11 and Wayland (one native target per backend).

Original executable, capture script, original JSON, source/build records,
red/green logs, native test reports and analyzer diagnostics are retained under
`E:/NewNotepad4/wxnotepad-evidence/completion-languages-2026-11-10/issue-193/`.
The reference capture and Windows tests use owned private desktops. X11/Wayland
use the repository's isolated display runner. No screenshot inspection or Linux
IME validation is claimed. The batch's independent final Spec/Standards reviews
and integrated platform checks are coordinated by the parent implementation task.
