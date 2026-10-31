# UTF-8 Open and document switching

Date: 2026-10-30. Scope: [#16](https://github.com/1298345095/wxnotepad/issues/16).

File > Open (Ctrl+O) uses the native existing-file chooser. File > New (Ctrl+N)
creates an unnamed, clean UTF-8 document without BOM, with CRLF insertion mode
matching the original unconfigured default recorded in #14. Open retains actual
CRLF, LF and mixed newline bytes; it stores the source path and UTF-8 BOM flag
separately from editor text. No encoding fallback is attempted.

The complete source is read and strictly validated before the current document
can be replaced. Invalid continuation bytes, overlong encodings, surrogate code
points, values above U+10FFFF and truncated sequences fail with an explicit
UTF-8 error. Missing files and directory sources also fail. Cancellation and
load failure preserve text, path, BOM, modified state, save point and Undo.
Successful Open/New clears prior document history and establishes a clean save
point.

Open, New and Close share the existing native Save / Discard / Cancel guard.
Cancelled or failed Save prevents switching. A successful explicit Save remains
effective even if a later load fails. After Save, Open reads the selected source
again before replacing the document, so reopening the current path shows the
newly saved content rather than the snapshot read before the prompt.

Tests use the approved application file-operation and real editor-state seams,
with independently read disk bytes. TDD red/green slices covered initial Open,
BOM metadata, strict rejection/state retention, native switching and menu
commands, and same-path reopen after Save. The first Linux directory-load
failure was fixed before the final backend runs.

`open_utf8` uses hidden frames for Unicode paths/text, BOM/no-BOM empty files,
mixed newlines, clean state, Undo isolation and cancelled/failed loads.
`open_new_native` activates actual process-local Windows/GTK widgets without
global keyboard or pointer input. It covers:

- Save / Discard / Cancel for Open and New, including cancelled first-save
  choosers, successful first/named Save and failed replacement with retained
  target/document/history.
- Native Open chooser acceptance/cancellation and New menu dispatch.
- Reopening the current path after Save.
- Eight exact byte roundtrips through the existing Close > Save path: empty,
  ASCII, CJK/non-BMP, CRLF, LF and mixed EOL, with and without BOM. An edit/delete
  leaves content unchanged while moving off the save point to exercise the
  actual close-Save write rather than just a clean close.

Validation after merging integration through `49ff0da`:

| Backend | Release build | Checks |
| --- | --- | --- |
| Windows 11, MSVC 14.51 | passed | full 7/7; final read-source fix: relevant 4/4 |
| Ubuntu 24.04 WSLg, GTK3 X11, GCC 13.3 | passed | final full 6/6 |
| Ubuntu 24.04 WSLg, GTK3 Wayland | same native Linux build | final full 6/6 |

All platforms use wxWidgets 3.3.3 and the pinned original core. Windows builds
live under `build/cmake`. Linux source, build, archive and temporary sample data
use native Linux storage under `/home/ctrls/.cache/wxnotepad-issue16/` and `/tmp`.
The last source copy preceded the final Linux rebuild and both backend runs.

Reproduce with `tools/build.py`, then CTest. `ctest -R 'open_utf8|open_new_native'
--output-on-failure` isolates this ticket; use `-C Release` on Windows. Linux
runs set `GDK_BACKEND=x11` and `GDK_BACKEND=wayland` explicitly. All focus-sensitive
checks on the shared desktop used the external `desktop_run.py` mutex wrapper.

The native-widget checks do not claim physical keyboard or Linux IME coverage.
Linux IME remains deferred. Save As, additional encodings, recent files and tabs
remain outside this ticket.
