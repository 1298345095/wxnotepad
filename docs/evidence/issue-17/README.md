# Save and Save As for existing documents

Date: 2026-10-30. Scope: [#17](https://github.com/1298345095/wxnotepad/issues/17).

File > Save (Ctrl+S) is enabled for both unnamed and named documents. Named
documents save directly to the current path; unnamed documents retain the
first-save chooser. File > Save As (Ctrl+Shift+S) uses the native save chooser,
initialized with the current directory/name, and native overwrite confirmation.

Save As passes the approved destination to the existing sibling-temporary-file
writer. Only a successful replacement changes document identity and establishes
the original core's save point. Cancellation and failure retain path, UTF-8 BOM
state, text, modified state and Undo/Redo history. Both commands preserve actual
newline bytes; Save As to a new path leaves the original file unchanged. There
is no encoding conversion in this change.

The original `FileSave` flow was inspected at pinned Notepad4 revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a` with CodeGraph. Its
`src/Notepad4.cpp:7112–7121` updates file identity after successful writing;
`src/SciCall.h:82–84` uses `SCI_SETSAVEPOINT`, which the integrated original core
already provides. This adapter follows that ordering and reuses the core's
history semantics. The original Win32/global-settings/MRU/lexer flow has no
smaller portable Save As helper to extract; the existing safe writer is retained
because the original direct-write path does not satisfy this phase's failure
preservation requirement.

TDD used the already approved application file-operation and real editor-state
seams, with independently read disk bytes. Red runs observed the disabled
existing-path Save menu, missing Save As operation, and missing native menu.
Each was implemented in a separate red/green slice.

`save_existing` uses hidden frames and covers:

- Existing-path Save through the menu, BOM/mixed-EOL bytes and unchanged identity.
- Save As to a Unicode path, original-file isolation, subsequent Save to the new
  identity, and Undo/Redo on both sides of the new save point.
- Cancel, missing parent and failed replacement, retained source/target bytes,
  retained history, and cleanup of sibling temporary files.
- Five exact Save As byte roundtrips: empty, BOM-only, no-BOM CRLF, BOM with
  CJK/non-BMP and LF, and no-BOM mixed CRLF/LF/CR.

`save_as_native` activates actual process-local Windows/GTK native widgets,
without global keyboard/pointer input. It checks both shortcut bindings and
menu dispatch, picker Cancel/new target, overwrite refusal/acceptance, failed
Save As after approved overwrite, and existing-path Save failure/error UI.
Windows failures use handles denying replacement; GTK failures remove directory
write permission at the overwrite confirmation. Disk bytes, identity, BOM,
modified state and Undo/Redo are checked after those outcomes. Windows overwrite
confirmation is identified by its owning chooser, so the system's localized
confirmation title is supported.

Final validation against integration `b72f3eb` (`git merge` reported up to date):

| Backend | Release build | CTest |
| --- | --- | --- |
| Windows 11, MSVC 14.51 | passed | 9/9 |
| Ubuntu 24.04 WSLg, GTK3 X11, GCC 13.3 | passed | 8/8 |
| Ubuntu 24.04 WSLg, GTK3 Wayland | same native Linux build | 8/8 |

All use wxWidgets 3.3.3 and the pinned original core. Linux sources/builds/archive
use `/home/ctrls/.cache/wxnotepad-issue17/`; sample data uses native temporary
storage. The final source copy preceded the final Linux rebuild and both backend
runs. Focus-sensitive runs used the external `desktop_run.py` mutex wrapper.

Reproduce with `tools/build.py`, then CTest. Use `ctest -R
'save_existing|save_as_native' --output-on-failure` for this ticket, adding
`-C Release` on Windows and explicit `GDK_BACKEND=x11` / `wayland` on Linux.
Shortcut checks validate native menu accelerator bindings, not physical keyboard
input. Linux IME remains deferred.
