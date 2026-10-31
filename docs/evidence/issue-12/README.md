# System text clipboard

Date: 2026-10-30. Scope: [#12](https://github.com/1298345095/wxnotepad/issues/12).
Pinned original core: `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`;
wxWidgets 3.3.3. Integration includes #11 and file operations through #17
(`9c2dd07`). No primary-selection, rich-text or drag/drop support is added.

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

Menus and Ctrl+C/X/V handlers are exercised through wx events. The tests do not
inject global keyboard or mouse input. Native focus is asserted for each peer
and editor action: Wayland clipboard selection is delivered to the focused
client. `tools/clipboard_focus.py` grants foreground only to PID-tagged fixture
windows, using native foreground APIs. It runs inside the shared desktop mutex
and requires a coordinated idle desktop. Its `--inspect` mode grants no focus;
the 15-second `WXNOTEPAD_CLIPBOARD_FOCUS_PROBE=1` fixture shows without activation,
detaches menus to avoid clipboard queries, and verifies published window identity.
WSLg adds `(Ubuntu)` to titles, and queued document-state notifications can
replace a fixture title; the focus request is reasserted until native activation.

Clipboard EOL representation differs across native APIs (wxMSW normalizes CRLF
to LF when reading a text object). Peer comparisons normalize CRLF to LF; editor
bytes are independently checked against literal CRLF results from the core's
configured paste conversion. This does not normalize saved files.

Validation status will be finalized after the coordinated native focus check.

## Environment observations

Windows clipboard contention was traced with GetOpenClipboardWindow to
PowerToys Mouse Without Borders Helper. The user temporarily closed it, and a
Release suite including clipboard and #17 passed 10/10. The user was then told
it could be restored; later state is unknown. Fixtures briefly yield after
publishing their own data to allow desktop clipboard synchronizers to finish.

An earlier Wayland sequence ran Copy and Paste successfully, then started the
external peer while the editor was inactive. Weston aborted with `*** stack
smashing detected ***` and signal 6, disconnecting GTK clients. This was not an
agent-requested restart. Independent stock wxTextCtrl Copy/Paste, public
wxClipboard text-object Copy/Paste, and a two-process stock roundtrip all passed.
Prestarting the peer avoided that reset but exposed stale clipboard availability
queries on the inactive editor. These observations distinguish the lifecycle
and focus conditions; they do not establish the cause of Weston's abort.
No compositor restart, system configuration change or product workaround was
introduced. WSLg [#1407](https://github.com/microsoft/wslg/issues/1407) describes
a related clipboard-bridge crash with a different assertion, so it is background
evidence rather than a confirmed match.

## Reproduction

Build with `python -X utf8 tools/build.py --source E:/NewNotepad4/notepad4` on
Windows. Linux sources, build and archive live under the native filesystem path
`/home/ctrls/.cache/wxnotepad-issue12-20261030`, built using `tools/build.py` and
the verified pinned archive. No Linux IME, package or proxy changes were made.

For a coordinated Windows check, run from the checkout:

```text
python -X utf8 E:/NewNotepad4/.wxnotepad-implementation-context/desktop_run.py python -X utf8 tools/clipboard_focus.py ctest --test-dir build/cmake -C Release --output-on-failure
```

For Linux replace the child command with:

```text
wsl.exe -d Ubuntu --exec env GDK_BACKEND=wayland ctest --test-dir /home/ctrls/.cache/wxnotepad-issue12-20261030/build/cmake --output-on-failure
```

Use `GDK_BACKEND=x11` for X11. Do not run these GUI suites concurrently or while
the user is working in another desktop application.
