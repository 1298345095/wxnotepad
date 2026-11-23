# Original INI, Makefile and Diff schemes

The catalog imports the pinned original `Ini Config File`, `Makefile` and
`Diff File` definitions. Their persisted identities are respectively 48, 45
and 50 (`EDITLEXER::rid - NP2LEX_TEXTFILE`), independent of catalog enum order.
The shared catalog supplies Scheme, F12, favorites/default selection, status,
Open/Save filters and the common font/style path.

Regenerate their complete original data with:

```text
python -X utf8 tools/update_schemes.py --source <original-checkout> --schemes INI Make Diff
```

INI and Diff both live in `stlDefault.cpp` and use `Keywords_NULL`. The importer
selects each `EDITLEXER` record by its style table before extracting metadata,
caption, extensions and resource identity. Makefile uses `stlMake.cpp`'s `MAK`
tables. Generated headers retain pinned source hashes and the existing
Notepad4 BSD license attribution. Grouped style IDs, null keyword lists,
keyword flags, end-of-line fill and all original editing metadata are retained.

The original `LexProps`, `LexMakefile` and `LexDiff` lexers supply coloring and
folding. INI has `;` line comments; Makefile has `#` line comments; neither has
block comments. Diff has no comment command. Makefile's completion word
characters include `!`, `$` and `-`, as in the original completion cache.
Keyword indentation uses the original case-sensitive first word and style/fold
gates: a newline inside an existing `define` body indents, while the captured
incomplete `define` at EOF does not gain an invented closing directive.

Makefile retains independent 4/4 literal-tab settings; INI and Diff inherit
global tabs by default. INI/Makefile retain original file-variable precedence.
On Diff load, the original clears detected and file-variable widths/spaces,
while retaining the independent Tab Indents variable. Explicit document
overrides and manual scheme changes preserve their existing precedence and
whitespace bytes. Mixed-EOL patch warnings default to No, preserving the input
when that button is accepted. No `GNUmakefile` alias is added. Case-insensitive `Makefile*` and
`Kbuild*` matches follow associated extensions. CMake's two `.txt` exceptions
retain priority; MySQL `my*.ini`/`my*.cnf` retain their original generic-config
sniffing behavior instead of becoming INI.
For unassociated filenames, the original bounded content guess can select INI
from a section at column zero. Its original C++/sharp-comment/JSON/YAML priority
checks and dotfile distinction are retained. This adds only the original INI
outcome to the shared fallback; existing explicit file associations still win.

The approved native evidence seams are real editor/application commands,
independently read document/settings bytes and history, and shown independent
application processes. `capture_build_patch_schemes.py` observes an owned pinned
original copy on its own Windows desktop. Its frozen fixtures cover Unicode,
CRLF/LF/mixed endings, INI sections/default values/comments, Make directives,
variables/recipes/continuations and Ninja rules, and unified/context/malformed
patches. Every original fixture retains base, incremental Delete, Undo and Redo
byte styles and full fold levels. Additional original records cover typed
newlines, paired insertion, comments, completion candidates, selection and
every Undo/Redo step. Regenerate with:

```text
python -X utf8 tests/reference_capture/capture_build_patch_schemes.py --exe <owned-original.exe> --output <new-evidence-directory>
```

`capture_build_patch_policies.py` independently records Diff's tab policy and
native mixed-EOL default buttons, with INI/Makefile controls. The native fixture
checks the resulting widths, spaces, Tab Indents, default button, saved bytes
and history. INI's content guess has eighteen independently captured startup
outcomes covering associations, dotfiles, competing content and the original
prefix limit. Default/Dark theme commands also verify original token colors
and grouped styles for every new scheme without editing the document.

`syntax_build_patch` replays these records through the native application and
checks exact saved bytes, original detection precedence, fonts/highlighting,
native Tab Settings persistence, New/first Save/Save As/reload and native file
dialogs. `build_patch_process.py` checks localized shown-app menus, native
edits, original default identities and restart in all three UI languages. The
shared localized F12/favorite layout and base-filter tests include these
catalog records automatically.

Implementation and validation records are retained under
`F:/wxnotepad-evidence/style-workflows-implementation-2026-11-23/268/`.
