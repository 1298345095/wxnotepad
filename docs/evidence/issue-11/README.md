# Navigation, selection and scrolling

Date: 2026-10-30. Scope: [#11](https://github.com/1298345095/wxnotepad/issues/11).
Original core remains pinned to `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`;
wxWidgets is 3.3.3. Branch starts from integration `292514d`.

The agreed test seam is the real editor control's wx input events and public
Scintilla commands, observed through document bytes, selection and viewport.
Three vertical TDD slices first failed on unhandled Right, then mouse drag,
then scrollbar input; each was implemented before adding the next slice.

`editor_input_undo` now covers UTF-8 Right/Shift+Right, selected-text replacement
and Undo; arrows, Home/End, Ctrl+Home/End, Ctrl+Shift+Home, select-all and deletion;
mouse hit/drag/release, multiline replacement and Undo; both scrollbar axes,
hit-testing after scrolling/resizing, and keyboard caret visibility. Literal
byte positions and document results are independent expected values. Home
alternates between indentation and physical line start, as in the pinned
Notepad4 `KeyMap.cxx` and `src/Notepad4.cpp` Home/End assignments; wrapping is off.
The host delegates character boundaries and selection movement to that core.

| Backend | Release build | Public event checks | Native OS input |
| --- | --- | --- | --- |
| Windows 11, VS 18 / MSVC 14.51 | passed | final CTest 2/2 passed | keyboard, mouse drag, both wheel axes and visible caret passed |
| Ubuntu 24.04 WSLg, GTK3 Wayland / GCC 13.3 | passed | editor suite passed before final long-line assertion refinement | keyboard, mouse drag, both wheel axes and visible caret passed |
| Ubuntu 24.04 WSLg, GTK3 X11 / GCC 13.3 | same Linux build | editor suite passed before final long-line assertion refinement | keyboard and mouse selection/edit/Undo passed; native scrolling awaits manual confirmation |

The final assertion refinement requires a nonempty long-line end to enter the
viewport (rather than the empty final line). The final Linux binaries are built;
rerunning that refined suite on both WSLg backends is pending the coordinated
desktop acceptance slot. Windows and Wayland native input already exercised this
long-line path. Native X11 Windows-injected wheel input did not reach the GTK
wheel event probe; this is not counted as a passing native scroll check. Its
public scrollbar-event/resize/hit-testing checks passed. No Linux IME was installed.

Reproduce builds with the root README's `tools/build.py` command. Linux source,
generated core, executable and reports are all on the Linux filesystem at
`/home/ctrls/.cache/wxnotepad-issue11`. Run the editor suite with
`GDK_BACKEND=wayland` or `GDK_BACKEND=x11` and CTest's `-R editor_input_undo`.

The separately built `navigation_probe` sends no input itself. It exposes the
public document/selection/viewport and hit-test coordinates to an external OS
driver. With a report path as its argument it opens `A中文\r\n  B\r\nlast`;
F6 loads the 100-line, long-line fixture. Add `--scroll` to start with that fixture
and keep the window available for five minutes. Manual X11 entry:

```text
wsl.exe -d Ubuntu --exec env GDK_BACKEND=x11 /home/ctrls/.cache/wxnotepad-issue11/build/cmake/navigation_probe /home/ctrls/.cache/wxnotepad-issue11/navigation-manual.txt --scroll
```

Temporary desktop drivers are retained outside the repository at
`E:/NewNotepad4/.wxnotepad-implementation-context/{desktop_run.py,win_ui.py,navigation_native.py}`.
All focused runs use the shared desktop mutex. The native driver verifies the
owned foreground window before each input batch. Captures in that directory
were visually assessed by the implementer: Windows/Wayland show the selected
Chinese character and the scrolled long-line end with its caret; X11 shows the
initial/selected text. Captures are not substituted for document assertions.
The driver accounts for the existing Windows DPI-unaware executable and WSLg's
different Wayland/X11 coordinate origins; cross-monitor DPI remains out of scope.

Single stream selection only. Clipboard, IME, multiple carets, rectangular
selection, text dragging and complex text layout remain separate tickets/scope.
