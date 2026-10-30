# First UTF-8 save

Date: 2026-10-30. Scope: [#14](https://github.com/1298345095/wxnotepad/issues/14).
Pinned Notepad4 core and wxWidgets versions remain unchanged.

New documents explicitly use UTF-8 without BOM and CRLF insertion on Windows,
Wayland and X11. Original source at `89b8b36` establishes these unconfigured
defaults: `Notepad4.cpp:5230–5239`, `EditEncoding.cpp:573–575`, and
`Edit.h:174–176,505`. Insertion mode does not normalize existing characters.
Saving writes the core's actual bytes, including mixed CRLF/LF/CR.

`DocumentSession` owns file identity and delegates text/history/save points to
the original core. A selected destination is written to a unique sibling temp,
then checked for full write, flush and close before Windows `MoveFileExW`
replacement or POSIX `rename`. Ordinary failures remove the temp; identity and
save point change only after replacement. Native Save uses
`wxFD_SAVE | wxFD_OVERWRITE_PROMPT`; errors from the menu display a readable
message. Existing named-document save remains #17; its menu command is disabled
in this stage. No Open, New, or general Save As flow is added.

The agreed test seam is application file operations, editor/document state and
independently read disk bytes; native dialogs are checked separately. TDD first
failed compilation on absent `SaveFirst`/identity/result interfaces, then passed
empty UTF-8 save. The next slice failed exact Unicode/mixed-EOL byte comparison
before adding editor-byte serialization. The native probe first failed on the
absent Save command before the menu/dialog implementation.

`first_utf8_save` runs with hidden frames and no global input. It checks an empty
file, literal Chinese/non-BMP/mixed-EOL bytes, a Unicode filename, identity/title,
and edit → Undo clean → Redo dirty around the actual save point. It checks
Cancel, missing-parent creation failure, post-write replacement failure against
an existing directory, and an existing sentinel file blocked by a Windows
non-delete-sharing handle or a Linux unwritable parent. Content, unnamed
identity, dirty state and history survive failure; sentinel bytes remain intact,
temps are removed, and an approved overwrite later succeeds.

| Platform | Release build | File-operation CTest | Native dialog paths |
| --- | --- | --- | --- |
| Windows 11 / MSVC 14.51 | passed | passed | new save, Cancel, deny overwrite then Cancel, accept overwrite |
| Ubuntu 24.04 WSLg / GTK3 Wayland / GCC 13.3 | passed | passed | same four paths |
| Ubuntu 24.04 WSLg / GTK3 X11 / GCC 13.3 | same Linux build | passed | same four paths |

Final regression CTest results: Windows 3/3, Wayland 3/3, X11 3/3 passed
(`editor_input_undo`, `display_repaint`, `first_utf8_save`).

Native probes use `file_smoke --native-save <expected-path>` or
`--native-cancel <expected-path>`. They dispatch the actual File → Save command,
wait for the real modal dialog, then assert identity and dirty state. Independent
Python reads compare exact saved bytes `Native 中文\r\nsecond\nthird\r` or the
unchanged sentinel. Windows used native filename entry and key responses;
Wayland used one-shot filename clipboard paste and key responses. X11 new save
and Cancel used the same native input route. Its overwrite prompt was visually
observed, but WSLg popup input routing was unreliable; the two overwrite branches
were completed via `--gtk-native-overwrite-no/yes <path>`, which set the real
chooser path and activate its actual native Cancel/Replace buttons. These are
directed GTK-widget checks, not physical keyboard coverage. No Linux IME was
installed or tested.

Windows build: repository `build/cmake`. Linux source, build, test data and
executables: `/home/ctrls/.cache/wxnotepad-issue14/` on the native Linux filesystem.
Run the root README build command, then `ctest -R first_utf8_save`; explicitly
set `GDK_BACKEND=wayland` and `GDK_BACKEND=x11` for Linux. Native probes require
a desktop; the GTK-directed modes can be bounded with `timeout 15` and require
an existing sentinel target to exercise overwrite confirmation.
