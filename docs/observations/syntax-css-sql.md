# Original CSS dialects and SQL

The standalone CSS, SCSS, Less and HSS entries use the pinned original
`LexCSS.cxx`, with `lexer.lang` values 0–3. SQL uses `LexSQL.cxx` with its
original default options: backtick identifiers, number-sign comments and
backslash escapes. Explicitly installing SQL's default is necessary because
the shared scheme installer clears the previous dialect property first.

All four CSS variants share the existing imported CSS keyword/style tables,
original `CSS Style Sheet` configuration section, and independent two-space
tab/indent defaults. SQL imports all six original keyword groups, their flags,
styles, editing metadata, extensions and `SQL Query` settings identity through
`tools/update_schemes.py`. Unknown original attributes fail the shared importer.
The source authority remains Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`; generated headers retain original
file hashes and the BSD notice. HTML/XML continue to use `LexHTML.cxx` and
the offset embedded CSS styles, with their existing markup dialect options.

`tests/reference_capture/capture_css_sql.py` runs an isolated copy of the
pinned original executable on an owned Windows desktop. Its independently
captured byte styles/fold levels and completion results are frozen under
`tests/data/css_sql*_reference.tsv`. The lexer oracle contains twenty states:
baseline, Delete, Undo and Redo for each of the five entries. Inputs cover CSS
selectors, custom properties, functions, strings/comments; dialect variables,
nesting/interpolation; SQL keyword groups, quoted identifiers, escaped/quoted
strings and incomplete syntax. Expected bytes come from the original process,
not the imported tables or the application's lexer.

`syntax_css_sql` opens actual files, compares all original style/fold bytes,
uses real Scheme/comment/fold/completion menus and native candidate selection,
and verifies naming, BOM/mixed EOL preservation and exact saved disk bytes.
It observes settled native paints before comparing incremental styles on
Wayland. CSS dialect switching and the shared persistent indentation identity
are exercised in the same document. `css_sql_process.py` additionally launches
the shown app in English, Simplified Chinese and Traditional Chinese, edits
through native input, saves, and verifies the same file after process restart.
The existing color CallTip fixture exercises preview pixels, native color
choices and exact Undo/Redo in each delivered scheme, alongside its original
plain-text typing/priority checks.

External evidence is retained at
`F:/wxnotepad-evidence/original-controls-implementation-2026-11-22/256/`, including
the independent original JSON, importer/native red runs and subsequent native
backend results. The file recognition tracer fails on the unmodified base;
the SQL differential tracer also caught a real missing default-option adaptation.
