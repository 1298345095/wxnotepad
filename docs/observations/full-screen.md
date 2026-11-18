# Fullscreen and window restoration

The pinned Windows Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a` was
observed through its owned private-desktop window, original menu commands and
queued F11/Esc events before implementing the port. At 144 DPI the normal
rectangle `(160, 130)–(940, 630)` covered the 2560×1600 monitor on entry. With
Hide Title set, the top window edge was −45 rather than −11. Both original bars
stayed visible until their own commands hid them. Esc cancelled completion and
then a CallTip without leaving full screen; the next Esc restored the normal
rectangle before the separately configured minimize action could run. The
first visible line stayed at 43. When the wider viewport fit the line, the
horizontal offset clamped from 35 to 0 and stayed there after restoration.

Save Settings Now omitted `FullScreenMode` while using the default policy,
even with a full-screen window. With just On Startup enabled it wrote
`FullScreenMode=1`; exit retained that value, and launch applied it with the
saved choices for both bars. The observed original maximized window kept its
maximized flag and work-area rectangle even while its resize frame was removed.
The port fills the monitor when entering from maximized and restores the saved
native maximized placement when leaving.

Windows uses the original monitor and DPI inset calculation to preserve or hide
its native title. GTK requests fullscreen from the X11 window manager or Wayland
compositor and retains the existing menu and status widgets. The shell owns the
window decorations in that state, so Hide Title is retained as a preference on
Linux. On unmanaged X11, wxWidgets' fallback is aligned to the current monitor.
Wayland never uses an absolute client position for entering or restoring.

`original_full_screen` and `original_full_screen_settings` exercise the owned
native window, keyboard and menu commands, editor state, settings file and
recreated frame. The X11 case also supports an EWMH-capable window manager to
verify maximized restoration. Run the tests through the native entry point in
[CI](../ci.md). The independent Windows samples and native run logs are under
`native-workflows-implementation-2026-11-18/237` in the task evidence, including
`original-03/observations.json`.
