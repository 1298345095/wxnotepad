# Read-only original-core host: actual checks

Date: 2026-10-30. Scope: [#9](https://github.com/1298345095/wxnotepad/issues/9).
Core: `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`; wxWidgets: `3.3.3#1`.

| Platform | Release build | Real GUI CTest | Actual display assessment |
| --- | --- | --- | --- |
| Windows 11, VS 18 / MSVC 14.51 | passed | 2/2 passed | ASCII and Chinese readable; resize and covering/uncovering preserve text |
| Ubuntu 24.04.5 WSLg, GTK3 Wayland, GCC 13.3 | passed | 2/2 passed with `GDK_BACKEND=wayland` | same readable sample and restoration |
| Ubuntu 24.04.5 WSLg, GTK3 X11, GCC 13.3 | same Linux executable | 2/2 passed with `GDK_BACKEND=x11` | same readable sample and restoration |

Windows used CMake's Release Visual Studio build and existing `G:/vcpkg`.
Linux used CMake 3.30.5/Ninja and `/home/ctrls/vcpkg`; source, generated core,
builds and executables were in `/home/ctrls/.cache/wxnotepad-issue09`, not `/mnt/e`.
No Linux IME was installed or tested.

The committed implementation was also exported with `git archive` into fresh
source directories and built separately on Windows and Linux with the documented
entry point. Those clean-source builds passed both GUI checks on Windows and on
each explicit Linux backend (2/2 each), using the same pinned source archive.

The first real-control test failed at `GetReadOnly` before the core's
`SetReadOnly` command was added. It then passed against the original sample's
known UTF-8 literal and a refused `ReplaceSel` command. The second process checks
native paint events after showing, resizing, covering/uncovering plus refresh,
and hiding/showing, while checking original document state. Compositors may
restore cached content on uncover without a paint event, so that test explicitly
refreshes too; paint events alone do not establish pixel correctness.

For the separate visual check, the real application was launched once per
backend, resized from 900×600 to 680×400, covered with another native window, and
uncovered. Actual Windows desktop pixels of the Windows/WSLg windows were
captured and visually assessed. These cropped captures retain the application
header and text after uncovering: [Windows](windows.png), [Wayland](wayland.png),
[X11](x11.png). They are not offscreen renderer snapshots. All three sample
lines remained readable and complete at the tested sizes.

Reproduce with the root README's build commands and the same visible operations.
The fixed text is inserted only into the original Scintilla document. The formal
host links the original sources plus the wx adapter, without wxSTC.

The source archive and patch apply were checked on both hosts. The original
Notepad4 checkout has no tracked changes. GCC reports existing `SplitVector` /
`RunStyles` optimization warnings in the pinned upstream; this ticket does not
change that code. Editing, clipboard, files, complex-script layout, IME and
performance are not results of these checks.
