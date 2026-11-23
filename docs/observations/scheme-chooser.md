# Native scheme selection and favorite filters

The reference is untouched Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. The private copied executable used for
these observations has SHA-256
`c4ae363348fe833b39c9d9b77f1f0b85565f140f7c5e52c51a0b91cf896227d9`.

`capture_scheme_chooser.py` drives the original `IDD_STYLESELECT` tree, default
and automatic-selection controls, normal/favorite OK, Cancel and reopening. Its
records retain the complete tree, selected item, checkbox state and exact
document/selection/modification/undo/redo state. Optional `--visible` records owned
window bounds with the dialog temporarily topmost. It does not use the clipboard.

`capture_scheme_filters.py` records actual native Open/Save filter lists, selected
indices and the unchanged Scheme menu. Four independently captured cases establish:

| Current | FavoriteSchemes | Open filter order, before the final platform wildcard suffixes |
| --- | --- | --- |
| Python Script | `67 1 32` | All Files, Python Script, Text File, 2nd Text File, Rust Source, C/C++ Source |
| Python Script | `32 1 67` | All Files, Python Script, Text File, 2nd Text File, C/C++ Source, Rust Source |
| Python Script | `67 1` | All Files, Python Script, Text File, 2nd Text File, Rust Source, C/C++ Source |
| Python Script | empty | All Files, Python Script, Text File, 2nd Text File |

Save has the same scheme order with All Files last. The current scheme appears
once even when also favorited; non-favorites are absent. All four Scheme menu
snapshots are identical. Sources are `Styles.cpp::Style_GetFileDialogFilter`,
`Style_UpdateSchemeMenu`, `Style_AddAllLexerToTreeView`, `Style_SelectLexerDlgProc`
and `Notepad4.rc::IDD_STYLESELECT` at the pinned revision.

The user approved this original filter policy for #254 after reviewing its
conflict with the previous all-catalog Save assertion. The delivered application
omits the unavailable 2nd Text File scheme and retains existing compact captions
for previously delivered schemes. New imported schemes use their original titles.
The shared base filter preserves an active dialect; the catalog carries the stable
original resource identity independently of enum positions.

The native `original_scheme_chooser` fixture covers selection, temporary default
state, fixed Text File, favorite checking/dragging, cancellation/reopening,
unavailable identities, both original capacity limits, and exact document/history
preservation. Actual mouse input runs on Windows and private X11/Wayland desktops;
F12 and Enter traverse the native keyboard paths. Three `original_scheme_layout_*`
cases measure both-axis tree growth, bottom-control placement and caption fit in
English, Simplified Chinese and Traditional Chinese. `original_scheme_file_filters`
compares the four captured ordering cases through actual native file dialogs.
`original_scheme_settings_lifecycle` drives the shown application across
New/Open/Save/Reload, menu checks, explicit/exit settings saves and process restart.

The existing native Save, Save Copy, GBK and markup scenarios explicitly favorite
the specific alternate types they exercise. Their byte, codec/BOM/EOL, selection,
history, cancellation, failed-write and type-result assertions remain intact. The
Save workflow additionally selects and saves every delivered base through its
current filter; there is no second production scheme registration list.

Machine-local captured JSON, PNGs, red/green logs, approved acceptance diff and
separate Spec/Standards reports are retained under
`F:/wxnotepad-evidence/original-controls-implementation-2026-11-22/254/`.
