# Native main-window appearance

Implements [issue #240](https://github.com/1298345095/wxnotepad/issues/240)
against Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.

The frame uses native menus, toolbar and status controls. The toolbar retains
the original default order and separators for delivered commands, routes the
existing command IDs, and supplies the original 16/24/32/40/48-pixel artwork.
Disabled artwork follows the original alpha blend against the native background.
The original BSD license remains in `LICENSES/`. The application and executable
icons use the original multiresolution resource.

`ShowToolbar` follows the existing settings lifecycle and Ctrl+F11 command.
Toolbar visibility is independent of the menu/status preferences and fullscreen.
The editor has no added border. The title follows the original filename,
`[directory]`, modified `* ` and read-only indications with wxnotepad's identity.
Native typography, colors and title decorations follow the active platform.

The thirteen status panes retain line/total lines, column, character, selection
characters/bytes, selected lines, matches, feedback/flexible space, scheme,
encoding/BOM, EOL, INS/OVR, zoom and size. The original scalar/tab rules and
rectangular `--` counts are preserved. Multiple-selection byte counts sum range
lengths and copy separators without materializing selected text. Character
counts reuse the editor's selection cache; line totals only rescan the current
line when its content, line or tab width changes.

Field widths use native text measurements. At narrow sizes the original left
counters and final size field have priority over metadata. wxMSW adds native
margins to zero-width panes; coincident native pane edges and cleared hidden
captions avoid clipped metadata fragments while preserving wx's logical text
for restoration. Status actions reuse the existing encoding, line-ending,
overtype, scheme, search, Goto Line and zoom commands and confirmations.

## Native lifecycle corrections

The reused Notepad4 core guards font realization with `fontsValid`; wxSTC's
older core always rebuilt fonts during style refresh. A DPI transition now
invalidates that flag as the pinned ScintillaWin host does. Real Windows guest
OS transitions on one HWND produced the same editor line heights as fresh
starts: 18/22/27/35 pixels at 100/125/150/200 percent, returning to 27 pixels at
150 percent. Consolas 11 points, zoom 100, document bytes and window identity
were preserved. The guest's original display scale and theme were restored.

GTK toolbar Paste updates use wxWidgets' asynchronous clipboard target query.
The synchronous query starts a nested event loop from idle UpdateUI; on
Wayland that loop could commit a maximized configure before GTK allocated its
new geometry, disconnecting the application. The asynchronous offer snapshot
retains the original core's read-only and protected-selection gates. Menus and
actual Paste commands retain their existing behavior. Native clipboard-offer
and repeated normal/maximized fullscreen fixtures cover this path.

The process observer waits for a completed initial native GTK frame before
querying UI state or forcing painting. Its existing timer retries without
publishing partial startup observations. This preserves Wayland presentation
and the original fixture deadlines and concurrency.

## Evidence

The ticket archive `next-native-file-tools-2026-11-18/240/` contains the
controlled original/base/after Windows captures, native status geometry and
text observations, and separate 52-case packaged X11 and Wayland matrices.
Those matrices cover normal/narrow/selected layouts, English/Simplified
Chinese/Traditional Chinese, scales 1/1.5/2, dark and HighContrast themes.
Windows VMware V3 includes 24 static frames at 100/125/150/200 percent and
five persistent-window OS scale transitions, with native DPI and editor metric
observations. `vmware/handoff.md`, `dpi-analysis.md` and `wayland-analysis.md`
record artifact hashes, reproductions and source-backed causes.

Linux decorations and native menu/control metrics differ from Windows by
design. The archived images record these differences; the application does
not replace the desktop's decorations or native theme rendering.
