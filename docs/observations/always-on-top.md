# Always On Top

The reference is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`:
`Notepad4.cpp::IsTopMost`, `LoadSettings`, `SaveSettings`, the
`IDM_VIEW_ALWAYSONTOP`/`IDT_VIEW_ALWAYSONTOP` command handler, command-line
parser and `GetRelaunchParameters`. English, Simplified Chinese and Traditional
Chinese captions come from the original `Notepad4.rc` files.

Always On Top is first in Settings > Window Options, with Alt+Shift+T.
`ToolbarButtons` identifies it as **26**: the original table includes separator
identity 0. Its bitmap index is **25**. The command shares one stable identity
between the menu and customizable toolbar; the original default layout does
not contain this optional button. Explicit duplicate buttons retain their order,
artwork, tooltip and actual pressed state.

The existing frame uses wxWidgets' `wxSTAY_ON_TOP`. On Windows wx updates the
native extended style and calls `SetWindowPos` with `HWND_TOPMOST` or
`HWND_NOTOPMOST`, preserving size, position and activation. On GTK it calls
`gtk_window_set_keep_above`. The application enables this action only on X11
when the WM advertises `_NET_WM_STATE_ABOVE`. It reads the native effective state
for menu/button checks. GTK Wayland currently has no supported client stacking
interface here: the command and toolbar button are disabled and unchecked, with
a translated explanation. The transferable configuration preference remains
available on disk.

The original `[Settings] AlwaysOnTop` preference defaults to `0`; saves omit the
default. A separate optional launch override handles `o` (on) and `o0`, `o-`,
`oo` (off), with the original Windows prefixes and Linux hyphen prefixes.
Saving without a manual toggle persists only the preference. A manual toggle
clears the override and sets the preference to the inverse effective state.
New Window and Restart load the saved preference independently.

GDK's cached window-state mask omitted ABOVE in a real Openbox run even though
`xprop` confirmed the actual `_NET_WM_STATE_ABOVE` property. Effective X11 state
therefore comes directly from that native property. The existing Alt+T handler
also consumed Alt+Shift+T before accelerator matching on GTK; its exact shifted
variant now calls the same topmost command while Alt+T retains Strip Trailing
Blanks.

`tests/always_on_top.cpp` exercises native menu dispatch, the real toolbar and
Alt+Shift+T. It reads the native topmost state and compares two owned top-level
windows in Windows' native Z order or X11's `_NET_CLIENT_LIST_STACKING`.
Raising the normal peer proves the editor remains above it when enabled and
returns to ordinary stacking when disabled. The same case retains document,
reversed selection, history, geometry, minimize/restore and fullscreen checks.
The existing Find window retains its native owner, stacking and query focus
while topmost is toggled; the real Go To Line modal remains above that owner.
The retained Microsoft Pinyin Escape fixture additionally toggles topmost during
actual preedit and checks the native context/layout, focus and both committed
and tentative document bytes before continuing its original Escape/Undo checks.
All three delivered languages run against the native fixture. The packaged
application is separately checked by `tests/always_on_top_process.py`.
Its lifecycle case covers both preference values against force-on, force-off and
no override, exit saving enabled/disabled, explicit saving, manual clearing,
New Window, relaunch, all original option prefixes/suffixes and absolute paths.
The same shipped program runs with portable and native installed configuration.

The native session helper retains its three-second Openbox readiness guard.
Between serial cases it clears the departed WM's stale supporting-window root
acknowledgement before starting the next owned WM. The focused
`managed_x11_readiness.py` case leaves a real stale acknowledgement, delays the
next real Openbox by 100 milliseconds and requires an actual supporting native
window before the child command can run. The unchanged original helper fails
this case with a stale-XID `BadWindow` error.

`tests/reference_capture/capture_always_on_top.py` takes an independent copy of
the pinned original. It records the executable hash, preference/override matrix,
explicit configuration writes, manual clearing and actual stacking against its
own original peer on an owned desktop. It never operates the user's editor.
The original capture uses the granted active input desktop, a registered normal
native peer and a real click whose hit-tested caption must belong to that peer.
This gives the normal peer actual foreground activation before comparing native
Z order. The resulting ten-row preference/override/manual-clear record is frozen
in `tests/always_on_top_reference.json`, with its executable hash and document
sample. The original Save Settings Now information dialog is verified and closed
before proceeding; its synchronous command must finish within the same guard.
