# Main-window positions

The behavior source is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`:
`LoadSettings`, `SaveWindowPosition`, `ClearWindowPositionHistory`,
`SnapToDefaultPos`, and `CMD_COPYWINPOS` in `Notepad4.cpp`.
`tools/capture_window_positions.py` captures an independent owned original
process's native rectangles, INI and clipboard, including a copied-argument
launch. It verifies the recorded reference executable's SHA-256 before use.

Windows history and `/pos x,y,width,height,maximized` use normal outer-window
dimensions in physical pixels and `WINDOWPLACEMENT` workspace coordinates.
The startup adapter adds the selected monitor's work-area offset when setting
a screen-coordinate rectangle. It fits the rectangle to an available monitor's
work area, including when the recorded display has disappeared. Explicit startup
sizes remain physical pixels across synchronous Windows DPI changes. Startup
without history uses the primary work area. The default-position command uses
the current monitor's work area with the original fixed 16-pixel
inset, the smaller of the available width and height, and right/left toggling.
Editor client dimensions and DIP font/toolbar dimensions are separate.

X11 uses the same original resolution-section names and five-field `-pos`
syntax. GDK/wx outer rectangles and monitor work areas are in logical screen
units, so a scaled X11 desktop uses those units consistently. Native tests
compare the owned X11 window and its frame extents, rather than assuming the
editor client rectangle is the outer frame. The X11 test session supplies an
owned Openbox for actual maximize/minimize/work-area behavior.

Wayland has no global window-position interface in the supported backend.
Copy Window Position is disabled, and Ctrl+Shift+K / Ctrl+Shift+P explain the
limitation in the status bar. Public position arguments still apply their size
and maximized state. Actual normal size and maximized state are retained in
`[wxnotepad Wayland Window]`; no fabricated coordinates are written, and
imported original resolution histories and generic coordinates are preserved.
Clear Position History removes the original resolution groups, Wayland size
history and the already-shared Run dialog width. A later explicit or exit save
can create a fresh record, as in the original. The next startup uses no-history
layout only while no subsequent save has created a record. The user explicitly
approved this original save lifecycle for #290.

Sticky Window Position first explicitly saves the current normal record, then
immediately writes `[Settings2] StickyWindowPosition`. Subsequent ordinary and
explicit saves retain that record. Fullscreen and minimized windows use the
last normal rectangle; maximizing retains normal dimensions and records its
maximized flag. Closing with exit saving disabled does not replace history.

Fullscreen Copy Window Position intentionally improves the pinned original:
the original copies its fullscreen rectangle (observed at 150% as
`/pos -11,-45,2582,1656,0`), while wxnotepad copies the cached ordinary rectangle
(`/pos 180,210,700,510,0` in the same sequence). The user explicitly approved
ordinary-position copying as an improvement for #290. Original fullscreen and
minimized INI persistence still retain the ordinary record.
