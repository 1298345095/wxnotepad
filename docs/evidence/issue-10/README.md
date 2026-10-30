# Basic input and Undo/Redo

Date: 2026-10-30. Scope: [#10](https://github.com/1298345095/wxnotepad/issues/10).
Pinned original core and wxWidgets versions are unchanged from #9.

| Platform | Release build | GUI CTest |
| --- | --- | --- |
| Windows 11, VS 18 / MSVC 14.51 | passed | 2/2 passed |
| Ubuntu 24.04 WSLg, GTK3 Wayland / GCC 13.3 | passed | 2/2 passed with `GDK_BACKEND=wayland` |
| Ubuntu 24.04 WSLg, GTK3 X11 / GCC 13.3 | same Linux executable | 2/2 passed with `GDK_BACKEND=x11` |

Windows uses the root README's existing dependency/build command. Linux sources,
generated core, builds and executables are in
`/home/ctrls/.cache/wxnotepad-issue10/source`; the pinned source archive is in the
sibling `data` directory. No Linux IME was installed or tested.

The agreed seam is the real editor control's public wx events and Scintilla
commands, with text, caret, history, save point and application title/menu state
as observations. The TDD slices first failed on the existing read-only sample,
then on unhandled Backspace, then on the absent frame/menu interface. Each slice
was implemented before the next was added.

`editor_input_undo` replays `wxEVT_CHAR` for the known literal `A中文` through the
actual character handler. The UTF-8 conversion buffer remains owned until
`InsertCharacter` returns. Backspace deletes a complete Chinese character;
Enter/Tab and Delete produce known literal text and caret positions. Ctrl+Z/Y
undo and redo the history, including crossings of the initial and a later
explicit save point. Native menu command events and update-UI processing check
Undo/Redo availability and the `*` title marker. These are event replay and
public-command assertions, not claims of native keyboard or IME coverage.

`display_repaint` transfers actual native focus to a temporary button and back,
checking the core's focus state. It then resizes, covers/uncover-refreshes, and
hides/shows the window while checking paint events, caret position, line count
and modified state. Focus transfer stays within one top-level window because
WSLg X11 does not reliably reactivate a window when another top-level window
closes. Paint events and state checks do not establish pixel correctness.

A separate Windows OS keyboard replay used the English (US) layout to type
`abc`, Enter, Tab, `d`, Backspace, `e`, Delete, then Ctrl+Z back to the initial
save point and Ctrl+Y forward. The title cleared/restored its marker. Actual
desktop pixels after cover/uncover showed `abc` and an indented `e` on the next
line. Dedicated Windows composition and candidate handling remain #13's work.

New documents currently retain the pinned core's default CRLF/Tab behavior;
this ticket does not select a new-file encoding or save policy. No clipboard,
file workflow or dedicated IME behavior is included.
