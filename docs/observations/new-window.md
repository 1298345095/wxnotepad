# New Window platform behavior

File → New Window and Alt+N start the current executable with native arguments:
`--window-position x,y,width,height,maximized`. The child uses ordinary startup
settings. The active document and process observation session remain in the
parent. The child's working directory is the parent's original startup directory.

On Windows, the original Notepad4 `GetRelaunchParameters(NewWindow | EmptyWindow)`
algorithm transfers `WINDOWPLACEMENT.rcNormalPosition`, adds 10 pixels on both
axes, and wraps a rectangle extending beyond the work area to the monitor origin.
Tests check that rectangle, maximized state, and the actual restored normal size.

On Linux, the application requests the matching normal size and maximized state
through wxWidgets; the compositor controls placement and decorations. The tests
check the received 640×480 normal-size request, actual normal-window geometry,
and actual maximized state on a capable display.

A standalone wxWidgets 3.3.3 / GTK 3.24 window on private Weston 13 starts maximized
from a 640×480 normal-size request and restores to 692×569 with client-side
decorations. Creating it with `wxMAXIMIZE` and showing it before `Maximize()` both
produce this dependency baseline. Linux tests retain the size request and native
maximization checks; this feature follows wxWidgets' decoration behavior after
the user later unmaximizes a window.

The normal process test, `original_new_window`, always exercises the native menu
and Alt+N. `original_new_window_maximized` exercises maximized inheritance.
On X11, its precondition is an advertised `_NET_WM_STATE_MAXIMIZED_VERT` and
`_NET_WM_STATE_MAXIMIZED_HORZ`. An Xvfb session without a window manager reports
the missing capability and returns CTest's explicit skip code 77 for this second
test. A display advertising those capabilities, and Wayland, must pass the actual
maximized-state assertion.

GTK launches use the existing GLib dependency's `g_spawn_async()` with owned UTF-8
argument buffers, inherited environment, original working directory and automatic
child reaping. Unlike the wxWidgets 3.3.3 Unix asynchronous `wxExecute()` path,
this returns failure when `exec` fails, allowing the parent to show its error.
The independent-process test removes executable permissions, checks that public
NewWindow reports failure, and verifies that the dirty parent and undo save point
remain usable. Windows retains the wide-argument `wxExecute()` path and copied
executable rename failure case.

The process runner also accepts `--scenario normal|maximized|all` and
`--installed-directory <owned-directory>` for user-configuration discovery tests.
The installation test creates its own private Unicode `XDG_CONFIG_HOME`.

Tools → Open in New Window (Alt+0) uses the same current-file relaunch path as
Restart, while keeping the parent open. It follows Restart in the original Tools
menu. Save Before Running Tools asks Save/Discard/Cancel for a modified document;
Save uses the ordinary Save As flow for an untitled document. Cancel or failed
Save stops the launch. Discard and disabled saving keep the parent buffer and
history, and the child reads the disk file. A pathless parent opens a blank child.

Current-file launches transfer the encoding identity, explicit scheme/dialect,
read-only editing mode, and 1-based visual caret line/column. Original Unicode
and ANSI switches select the current encoding after automatic source decoding;
legacy switches select source decoding. Both UTF-16 variants select the matching
BOM format on relaunch, including a parent using UTF-16 without a BOM. Settings
and UI language follow ordinary startup discovery. Placement and the original
startup working directory use the existing New Window launch path above.

The pinned original reference is Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, the shared
`IDM_FILE_NEWWINDOW` / `IDM_FILE_NEWWINDOW2` / `IDM_FILE_RESTART` branch and
`GetRelaunchParameters`. Native packaged tests observe both independent
processes through the existing shown-application seam and read actual saved
bytes; original observations are retained under
`F:/wxnotepad-evidence/style-workflows-implementation-2026-11-23/266/`.

One explicit dialect difference is retained by the approved behavior: the pinned
original serializes the Shell Script family name for an explicit C Shell parent,
and its child reports Shell Script. wxnotepad's existing Restart relaunch path
transfers the effective C Shell dialect; Open in New Window reuses that behavior.
The packaged current-file tests require C Shell in the child. This is not claimed
as exact original parity for that dialect.
