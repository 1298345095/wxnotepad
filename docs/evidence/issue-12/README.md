# System text clipboard

Date: 2026-10-30. Scope: [#12](https://github.com/1298345095/wxnotepad/issues/12).
Pinned original core: `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`;
wxWidgets 3.3.3. Integration includes #11 and file/codec operations through #19
(`390f5c22`). No primary-selection, rich-text or drag/drop support is added.

| Environment | Final result |
| --- | --- |
| Windows 11, wxMSW / MSVC 14.51 Release | native system clipboard case passed, 6.68 s; latest Release build passed |
| Ubuntu 24.04, GTK3 X11 on private Xvfb | complete integrated CTest passed 12/12, 22.13 s |
| Ubuntu 24.04, GTK3 Wayland on private Weston 13 X11/Pixman | complete integrated CTest passed 12/12, 22.99 s |

The unchanged Windows path also passed an earlier complete suite through #17
(10/10). The final Linux suites include merged UTF-8, UTF-16LE and GBK flows.

The agreed seam is the real editor control's public commands and wx input/menu
events, observed through document bytes, selection, Undo/Redo and save points.
`clipboard_peer` is an independent application linked only to wxWidgets; its
stock native text control copies and pastes through the actual system clipboard.
Fixtures include English, Chinese, an emoji, and multiple lines. IPC contains
only the test's own text and command/status records. Previous user clipboard
contents are neither read into evidence nor logged.

TDD slices first failed on missing Copy, missing Paste, deletion after a failed
Cut copy, and missing menu/shortcut behavior. Each slice was implemented before
the next test. Native X11 additionally exposed corrupted paste bytes when only
the result of `GetText().ToUTF8()` was retained: on a UTF-8-native wx build that
result can borrow the temporary source string. Paste now retains the source
`wxString` and conversion object for the whole core insertion. Each Cut or Paste
is one Undo operation; replacing a selection and Undo/Redo across a save point
preserve the core's modified state. A real Windows peer holding OpenClipboard
verifies that failed Copy prevents Cut deletion and leaves history unchanged.

With a confirmed native input gesture, repeat wx legacy publication on the
isolated Wayland display returned empty text while selection stayed unchanged.
GTK publication now uses `gtk_clipboard_set_with_data`: each UTF-8 snapshot has
its own GTK clear callback, and Cut checks GTK's actual success result. The
public regression then passed. Installed wx 3.3.3 `gtk/clipbrd.cpp` replaces
`Data()` in AddData and its selection-clear callback clears the global current
data; this supports the ownership diagnosis. Related wx issues
[#16636](https://github.com/wxWidgets/wxWidgets/issues/16636) and
[#26265](https://github.com/wxWidgets/wxWidgets/issues/26265) describe reentry and
replacement problems; the latter's explicit Clear bug is already fixed in the
installed source. The peer publishes its single custom non-text offer with wx,
then uses stock native wxTextCtrl Copy for text transitions.

Menus and Ctrl+C/X/V handlers are exercised through wx events. The tests do not
inject host keyboard or mouse input. Native IsActive/HasFocus is asserted for
each application. Wayland ownership changes receive a real private-display
modifier gesture, confirmed by a native wx key-up event; serials/focus are not
fabricated. Tests wait for actual new text/custom MIME offers and compare
CanPaste against independent GTK availability before checking menus.

Clipboard EOL representation differs across native APIs (wxMSW normalizes CRLF
to LF when reading a text object). Peer comparisons normalize CRLF to LF; editor
bytes are independently checked against literal CRLF results from the core's
configured paste conversion. This does not normalize saved files.

## Environment observations

Windows clipboard contention was traced with GetOpenClipboardWindow to
PowerToys Mouse Without Borders Helper. The user temporarily closed it, and a
Release suite including clipboard and #17 passed 10/10. The user was then told
it could be restored; later state is unknown. Fixtures briefly yield after
publishing their own data to allow desktop clipboard synchronizers to finish.

Earlier combined WSLg runs aborted Weston 9 with `stack smashing detected` and
signal 6, including a focused X11 run at 18:02:22.362 after both format checks
passed. No agent-requested compositor restart occurred. WSLg
[#1407](https://github.com/microsoft/wslg/issues/1407) reports a related clipboard
bridge crash with a different assertion. Its exact cause remains unproven;
the bridge sequence was not retested after the GTK publication fix.

The user supplied Xvfb, Weston 13 and xdotool. Final Linux checks use private
authenticated Xvfb, a private DBus session, and a 0700 runtime/socket. Wayland
uses Weston's X11 backend and Pixman; xdotool touches only that private DISPLAY.
Independent stock two-process roundtrips passed before application checks.
The runner cleans only its owned process groups/runtime. This exercises real
X11/Wayland clipboard protocols while excluding the Windows RDP clipboard bridge.

## Reproduction

Build with `python -X utf8 tools/build.py --source E:/NewNotepad4/notepad4` on
Windows. Linux sources, build and archive live under the native filesystem path
`/home/ctrls/.cache/wxnotepad-issue12-20261030`, built using `tools/build.py` and
the verified pinned archive. Linux IME remains deferred; no proxy change was made.

For a coordinated Windows check, run from the checkout:

```text
python -X utf8 E:/NewNotepad4/.wxnotepad-implementation-context/desktop_run.py python -X utf8 tools/clipboard_focus.py ctest --test-dir build/cmake -C Release -R system_clipboard --output-on-failure
```

For Linux, inside Ubuntu with native sources/builds:

```text
xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' dbus-run-session -- python3 /home/ctrls/.cache/wxnotepad-issue12-20261030/src/tools/clipboard_isolated.py wayland ctest --test-dir /home/ctrls/.cache/wxnotepad-issue12-20261030/build/cmake --output-on-failure
```

Replace the runner's `wayland` argument with `x11` for X11. Windows foreground
checks need a coordinated idle desktop; the isolated Linux runs do not.
