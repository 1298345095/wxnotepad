# Window routing

The routing contract follows Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, its `LoadFlags`,
`ActivatePrevInst` and `ActivatePrevWindow` branches, and 81 independently
captured original-process observations. The frozen 60-row original matrix is
`tests/window_routing_reference.json`; it is independent of the product router.

Settings > Window Options > Reuse Window defaults off. Settings > Single File
Instance defaults on. Each command immediately writes its own `[Settings2]`
key, including when Save Settings On Exit is off. Default values remove the
key. Ordinary Save Settings Now and close preserve the immediate values,
including values changed by another window.

| Reuse | Single file | No file | Same file | Different file |
| --- | --- | --- | --- | --- |
| Off | Off | New | New | New |
| Off | On | New | Activate same | New |
| On | Off | Activate available | Open in available | Open in available |
| On | On | Activate available | Activate same | New |

`-n`, `-ns`, `-r`, and `-rs` replace both flags with the corresponding row,
independently of persisted settings; the last switch wins. Windows also accepts
`/` and `--`; Linux accepts `-` and `--`. A failed same-file search never falls
back to arbitrary reuse. A same-file match applies no payload and retains the
unsaved buffer, undo history, caret, mode, encoding and pathname identity.

Opening another file uses the ordinary Save/Discard/Cancel and failed-save
paths. Cancellation and open failure are handled requests: the launcher exits
without opening again. As in the original receiver, `-g`/`-G` still move the
old document's caret after a canceled replacement. Encoding, scheme and
read-only options apply only after a successful open. Alt+N, Alt+0, Restart and
language restart explicitly launch with `-n`.

One original defect is corrected to satisfy issue #277: original forwarding
transmits a named scheme with a leading dot, while its receiver's extension
matcher expects a bare extension. The independent `/r /s cpp` observation
therefore remains plain text. wxnotepad forwards the launcher-resolved scheme
and applies C++, as the approved ticket requires.

Windows receivers use an application-specific hidden window and versioned
`WM_COPYDATA` on the current native desktop, with same-user discovery. Linux
uses the installed static `wx::net` Unix-domain transport: one socket per
process in a directory owned by the user with mode 0700, scoped by the actual
X11/Wayland display and login session. Orderly exit removes only the owned
socket. Queries skip stale endpoints; uncertain delivery is never retried.
The reply distinguishes activation, successful open, canceled open, failed
open, busy rejection and closing rejection. GTK's native modal grab leaves
the frame's logical enabled flag true, so busy detection includes wx's actual
modal-dialog count.

Delivery and activation are separate observations. Windows grants foreground
permission to the receiver and performs one wx `Raise` call, which calls
`SetForegroundWindow`. Tests check the actual foreground HWND and native
editor focus after delivery, without activating the receiver from the test.
X11 uses its window manager; Wayland presentation remains subject to compositor
policy. The tests retain delivered/shown/active/focus observations separately
and use actual independent X11, Wayland and Win32 desktop sessions for isolation.

Desktop startup IDs are request metadata, including for same-file activation.
They are captured before `wxApp::Initialize`, because GDK consumes/unsets
`DESKTOP_STARTUP_ID` during initialization. Wayland selects
`XDG_ACTIVATION_TOKEN` when supplied, otherwise the GTK startup ID; X11 selects
the desktop startup ID. The receiver gives Wayland IDs to
`gdk_wayland_display_set_startup_notification_id` before its one `Raise`, and
X11 IDs to the public GTK startup-ID/user-time APIs. IDs are never saved in
settings or inherited as consumed XDG activation tokens by relaunched children.

The native tests obtain IDs from the real
`GdkAppLaunchContext.get_startup_notify_id` caller. On X11 the receiver's
`_NET_STARTUP_ID` property exactly matches each issued ID. The private Weston
probe, using Python GI with `Gio.AppInfoCreateFlags.NONE` and a direct
`get_startup_notify_id` call, returned `None`. This does not establish why no
token was returned or a compositor/protocol refusal. Its no-token minimized case
records successful delivery with the actual surface still unmapped and
inactive; it does not claim that the compositor granted restoration. Positive
activation with a compositor-issued Wayland token is not established by this
Weston configuration. GTK 3.24.52's native focus path consumes the supplied
startup ID and uses `xdg_activation_v1.activate` when that protocol is supported.
