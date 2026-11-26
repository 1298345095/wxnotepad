# CSV and TSV

The CSV scheme uses Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a` directly: `scintilla/lexers/LexCSV.cxx`,
`src/EditLexers/stlDefault.cpp`, and `src/Styles.cpp` (`Style_SniffCSV` and
`SelectCSVOptionsDlgProc`). Its BSD 3-Clause license is in `licenses/Notepad4.txt`.
The shared offline scheme importer retains its original ID 80, eleven column and
delimiter styles, no-keyword table, plain-text font inheritance, and edit metadata.

The options belong to the running document session. They are not saved in the
configuration. The Scheme menu, a newly selected F12/favorite entry, and a CSV
status-pane double-click open the original fixed Options layout. Only OK is
visible; closing the window or Escape cancels. Automatic CSV detection samples
the first two physical lines;
TSV chooses Tab. A same-path reload retains manual options. A new automatic CSV
selection sniffs again; when the sample has no candidate delimiter the previous
options survive, as in the original.

`tests/reference_capture/capture_csv.py` observes an owned, untouched pinned
original executable through its public editor messages. It captures complete
style bytes, line states and fold levels, before and after an actual deletion,
Undo and Redo. The checked-in fixture covers column wrapping, empty fields,
quoted separators/newlines, doubled quotes, unfinished quotes, Unicode,
CR/LF/CRLF, TSV, sniff priority and 205 rows. The `--options` capture adds all
six delimiters, single/no qualifier, backslash escaping, merged delimiters and
custom first-character/invalid-character behavior (18 fixtures, 72 complete
states total). `tests/data/csv_reference.provenance.json` records executable and
capture hashes. Regenerate the table offline with
`python tools/csv_reference.py original.json tests/data/csv_reference.tsv`.
Scheme/style data regenerate with `tools/update_schemes.py --source <original>
--schemes CSV` and `tools/style_profiles.py --source <original>`.


Native regressions compare both incremental and full recoloring against those
independent states. They also cover text-font/code-font inheritance, cancellation,
unchanged confirmation, theme/scheme transitions, read-only mode, Undo/Redo and
exact saved bytes, real F12/favorite ID 80, native Open/Save As filters, and manual
Plain Text pause/resume above 32 MiB. Automatic and All Files Save As reapply a
changed CSV option word even when the scheme identity stays CSV; TSV still
forces Tab without rewriting the saved data. The existing style-customization
regression imports/exports CSV Column 1, Column 10 and Delimiter through its native
dialogs while retaining all previous assertions.

The actual packaged-process regression exercises CSV/TSV open, native options,
edit/save, fresh-process defaults and English/zh-CN/zh-TW layout. The UI uses
contextual Tab/Space translations because these denote delimiter characters,
while other dialogs use the same English words for keyboard keys. Windows also
runs GDI/Direct2D and complementary native 96-DPI layout cases. Registrations are
appended to `cmake/tests/NativeRegressions.cmake`; all previous case deadlines and
ordering remain intact.
