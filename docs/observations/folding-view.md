# Folding visibility and current-block markers

Reference: Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, issue #152.
The original executable is the existing `build/notepad4-baseline/Notepad4.exe`;
`notepad4-baseline.log:224` records its build output, and the reference checkout
still resolves to the pinned revision.

## Original observations

The preserved capture script and JSON records are under
`E:/NewNotepad4/wxnotepad-evidence/editor-navigation-controls-2026-11-07/152/`.
`original-gdi.json`, `original-directwrite.json`, and `original-gdi96.json`
contain the source fixtures, lexer/fold levels, native margin clicks, menu state,
every line's expansion/visibility, directed selections, zoom and resize results.
The probes run on owned Windows desktops without activating the user's desktop.

- Both preferences default enabled. Hiding folding sets its margin width to zero
  and expands every nested and unrelated header. Showing it retains that expanded
  state. The fold menu remains enabled; its commands do nothing while folding is
  hidden. Plain Text retains the folding margin and uses indentation folding.
- The default width is `TextWidth(StylesCommon::LineNumber, "+_")`. The pinned
  core's line-number style is **32**. At 144 DPI, the original C++, Python and JSON
  fixtures measure 22 pixels at 100% and 33 at 150%. Plain Text measures 21 and 32.
  With the original process's `DPIUNAWARE` compatibility layer, it reports 96 DPI:
  the same code fixtures measure 14 and 20 pixels; Plain Text measures 13 and 21.
  Resizing retains the measured width.
- Numeric original paint samples use the original GDI rendering preference
  (`RenderingTechnology=0`) and `PrintWindow` into an owned compatible DC.
  The default DirectWrite process supplies the separate state/geometry capture.
  Normal folding lines are RGB `#8080FF`, selected lines `#FF0000`, collapsed-box
  fill `#ADD8E6`, and the default margin background is white. Selecting an outer
  block also emphasizes its nested connecting lines; a nested body emphasizes
  that nested block, and a caret after the outer block clears the emphasis.
- The original `Editor.cxx` gates `CanUndo` and `CanRedo` with read-only mode.
  Reenabling editing exposes the existing history again; view changes retain it.

## Implementation and checks

Margins follow the shared original layout: line numbers 0, bookmarks 1, folding 2.
The editor retains the two preferences and delegates current-block selection to
the pinned core's `MarkerEnableHighlight`; it does not reconstruct fold trees.
Font/style, zoom and DPI invalidation remeasure folding width. Native commands and
settings use `ShowCodeFolding` and `HighlightCurrentBlock` with original true defaults.

The registered `original_folding_view` test exercises real native menus,
Ctrl+Shift+Alt+F, queried margin clicks, actual widget paint, nested caret movement,
resize, hidden line numbers, C++/Python/JSON/header-free Plain Text, read-only
selection, dirty bytes, Undo and Redo. Windows also runs the complementary
`original_folding_view_dpi96` unaware-thread fixture. The independent
`original_folding_settings` processes cover seed import, boolean-prefix parsing,
explicit save, exit-save enablement, restart, New/Open and retained INI fields.

Windows red/green logs are preserved beside the captures: visibility, geometry,
paint, settings and hidden-command gates each failed before their implementation.
`final-tests.log` records the nine passing folding/line-number/wrap/syntax/settings
regressions. `final-view-tests.log` records both final visual fixtures passing after
the read-only history assertion was aligned with the original public API.
Canonical clang-format 23.1.2 and `git diff --check` pass. The parent integration
run owns the final X11/Wayland, full Windows, clang-tidy and independent review.

## Toggle Folds commands (#234)

View → Toggle Folds provides Default Levels (`Shift+Alt+D`), All Levels
(`Shift+Alt+A`), Current Block (`Alt+C`), Current Level, and Levels 1–10.
`Alt+1` through `Alt+9` select their matching levels. The submenu stays
enabled when the folding margin is hidden; the commands do nothing in that
state. The commands add no settings.

All Levels delegates to the pinned core's toggle with contraction at every
level. Individual level commands choose their direction from the first header
whose expanded state actually changes. Indentation-based lexers count parent
headers; other lexers use their numeric fold levels. Default Levels applies
the selected scheme's mask and style exclusion after styling the document.

The pinned original executable was run before counterpart changes in seven
isolated Windows processes. The capture script is
`tests/reference_capture/capture_fold_commands.py`; the raw state, menu tree,
SHA-256, and inputs are in
`F:/wxnotepad-evidence/native-workflows-implementation-2026-11-18/234/original-01/original.json`.
The registered `original_toggle_folds` fixture replays all 78 transitions
through the native menu and repeats them with the original shortcuts where
assigned. The frozen data include each line's expansion and visibility,
directed selection, viewport, bytes, save point, and Undo/Redo. Cases cover
C++, uneven Python indentation, Markdown, dirty history, a scrolled view, and
a header without effective children. The menu hierarchy test also checks the
original ordering and separator.

On the original, All Levels can hide both selection endpoints without moving
them. This is independent of the older explicit `FoldAll` API's wxnotepad
selection repair: the native replay checks the original offsets, and the two
legacy repair assertions still exercise the explicit API. Margin clicks,
block navigation, and the boxed fold display retain their existing checks.
