# Original Markdown dialects

Markdown uses the original `LexMarkdown.cxx` at Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. The only lexer-source adaptation is
an explicit `<climits>` include for its existing `INT_MIN` use on GCC. The
lexer algorithm, dialect values (GitHub 0, GitLab 1, Pandoc 2), byte styles and
heading folding are unchanged. Copyright and license remain in
`licenses/Notepad4.txt`.

`python tools/update_schemes.py --source <original-checkout> --schemes Markdown`
reproduces the table through the shared importer. The original extensions are
`md; markdown; mdown; mkdn; mkd; litcoffee; rmd; jmd`; associated filenames take
precedence over content sniffing. GitHub GFM is the original automatic dialect.
The three native Scheme choices share the original `Markdown` settings section,
tab defaults and one Markdown Save As filter. Choosing its current filter keeps
the active dialect; All Files redetects from the destination, while an explicit
scheme overrides the extension. Same-path reopen keeps an explicit choice.

The pinned Markdown style table uses foreground, background, bold, italic,
underline, strike, overline and filled line backgrounds. It contains no font or
size overrides. All styles retain the editor's native point-sized font. Shared
metadata and installation carry all these attributes to the original core's
existing style APIs; scheme changes reset flags before installing the next
table. Original global link colors and HTML styles through `SCE_H_QUESTION`
are also installed, matching `Styles.cpp::Style_SetLexer`. Completion masks are
ported from `EditAutoC.cpp::InitAutoCompletionCache`, including its embedded
HTML comment/string and Markdown plain-text masks.

## Independent observations and public workflows

`tests/reference_capture/capture_markdown.py <pinned-exe> <evidence-directory>`
uses an owned private Windows desktop and portable INI. It freezes the original
executable's bytes, every byte's style and full fold levels. The retained
`tests/data/markdown_reference.tsv` has 48 states: base, one byte deletion, Undo
and Redo for the three dialects across the main syntax fixture and separate
TOML, JSON and Pandoc-title front matter. The main fixture includes all heading
levels, Setext headings, tasks/lists, quotes, code, links, emphasis, strike,
tables, escapes, math, metadata, embedded HTML and incomplete constructs.

`original_markdown_dialects` drives actual Open, Delete, Undo/Redo, Scheme
selection, highlight suspension/resume, native Open/Save As, saved disk bytes,
shared Tab Settings and persistence, comments, collapse/expand, native Find
unfolding and block navigation. It scrolls the actual viewport to observe the
complete incremental style pass rather than replacing it with a full Colourise.
Public style queries check attributes and preserved point sizes. Native pixels
prove the installed superscript overline is drawn. Set
`WXNOTEPAD_MARKDOWN_SAMPLE` to an external PNG filename to retain a native
sample; this records the window's DPI, scale factor, font point size and line
height without model image inspection.

Red/green logs, original executable/captures, native style sample, source and
binary evidence are archived under
`E:/NewNotepad4/wxnotepad-evidence/completion-languages-2026-11-10/issue-195/`.
Windows observed 144 DPI with a 12-point font and 28-pixel line height. Native
backend results and analyzer diagnostics are retained with that evidence. The
parent task owns final combined Spec/Standards reviews and integrated platform
verification; no Linux IME or model screenshot inspection is claimed here.

Local validation passed the Markdown workflow on private Windows, X11 and
Wayland. Canonical clang-format 23.1.2 passed all tracked C/C++ sources. Native
clang-tidy 23 (Windows) and 18 (Linux) passed the seven affected translation units
under the repository analyzer error gate; generated signed-mask and inherited
API warnings remain in the logs. Python discovery passed 30 tests with three
platform skips using UTF-8 mode, and both the Markdown and shared import CLI
checks passed. A stale Linux archive timestamp initially left incompatible
settings objects; rebuilding current headers resolved it. The GTK file-filter
regression now checks the original single Markdown type rather than counting
its two additional Scheme-only dialect choices as file types.

After merging completion integration through `9039c90` (including #191 and
#194), the rebuilt five-target matrix passed 5/5 on private Windows, 5/5 on X11
and 5/5 on Wayland: `original_markdown_dialects`,
`original_toml_yaml_cmake_schemes`, `original_rust_go_lua_schemes`,
`original_markup_schemes`, and `automatic_language_save`. Both native analyzers
passed the seven affected units again. The final Windows style sample is
`windows-native-style-final.png`; its run passed at 144 DPI, 12 points and a
28-pixel line height. `evidence-record.json` records source/binary/reference
hashes. The generic reference-process helper is shared with the configuration
language capture utilities, and recapturing Markdown after that merge produced
identical frozen snapshots.
