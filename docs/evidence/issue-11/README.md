# Navigation, selection and scrolling

Date: 2026-10-30. Scope: [#11](https://github.com/1298345095/wxnotepad/issues/11).
Original core: `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`; wxWidgets: 3.3.3.
Started from integration `292514d`, then merged #14, #13 and #15 through `a127588`.
IME caret updates and navigation/scroll callbacks are both preserved.

The agreed seam is the real editor control's wx input events and public
Scintilla commands, observed through document bytes, selection and viewport.
Three TDD slices first failed on unhandled Right, mouse drag and scrollbar input;
each was implemented before adding the next slice. Literal expected results
cover UTF-8 movement, Shift selection, Home/End and document boundaries,
select-all, replacement/deletion and Undo, mouse hit/drag/release, both scroll
axes, hit-testing after resize, and revealing a nonempty long-line caret.
Home's indentation/physical-start toggle matches the pinned Notepad4 key map
and Home/End assignments with wrapping off. Character boundaries stay in the core.

| Backend | Final Release build / integrated CTest | Native interaction |
| --- | --- | --- |
| Windows 11, VS 18 / MSVC 14.51 | passed; 5/5 | OS keyboard UTF-8 selection/edit/Undo, multiline mouse drag, both wheel axes, visible caret passed |
| Ubuntu 24.04 WSLg, GTK3 Wayland / GCC 13.3 | passed; 4/4 | same native keyboard/mouse/scroll/caret sequence passed |
| Ubuntu 24.04 WSLg, GTK3 X11 / GCC 13.3 | same Linux executable; 4/4 | native keyboard/drag/edit/Undo passed; human scrollbar drag, selection after scroll and normal window resizing passed |

The final X11 display check explicitly skipped only native-focus assertions
because the desktop did not activate its top-level window. Paint, resize,
cover/uncover, hide/show and caret/document preservation still ran and passed.
Windows and Wayland focus assertions ran and passed. Earlier successful native
focus evidence remains in #10; no fake focus event or product activation change
was introduced. Final integrated suites also cover the merged IME geometry
(Windows), first-save and unsaved-close behavior.

During mixed WSLg backend tests the human observed an invisible pointer and
could not initiate edge resize, including over a stock native GTK text field.
Native diagnostics found no residual capture, an opaque I-beam and enabled
resize flags. The official WSLg repository has a
[similar cross-application cursor-loss report](https://github.com/microsoft/wslg/issues/1424#issuecomment-6249999698)
and [display-client reconnect recovery reports](https://github.com/microsoft/wslg/issues/1424#issuecomment-4442947473).
With other GUI work quiescent, the root authorized one reconnect of the exact
verified `C:\Program Files\WSL\msrdc.exe` client whose command line contained
`/wslg` and whose visible-window inventory was empty. It automatically respawned;
Ubuntu stayed alive. Human retest of the normal native window then confirmed a
visible pointer and normal drag resizing. This records a successful recovery,
not a universal root-cause claim. No WSL shutdown, install, configuration edit or
production cursor/chrome workaround occurred. The resize cursor remaining an
ordinary arrow was accepted as a cosmetic native/WSLg appearance difference.
Local versions were WSL 2.7.8.0, WSLg 1.0.73.2 and MSRDC 1.2.6676.

Build with the root README's `tools/build.py` command. Linux source, generated
core, binaries and reports are in `/home/ctrls/.cache/wxnotepad-issue11`; run CTest
with explicit `GDK_BACKEND=x11` or `GDK_BACKEND=wayland`. Native drivers and
captures are retained outside the repository in
`E:/NewNotepad4/.wxnotepad-implementation-context` (`desktop_run.py`, `win_ui.py`,
`navigation_native.py`). Focused runs use the shared desktop mutex; the OS input
driver checks its owned foreground window before each batch.

`navigation_probe <report-path> --scroll` opens the 100-line long-line fixture
for up to five minutes with an explicit **Close probe** button. F6 reloads it;
Ctrl+End then Up/End reveals a long-line end, Ctrl+Home returns to the start.
The test-only `--compare`/`--compare-csd` modes retain the native-text differential
fixture; they do not change the production frame. Captures were assessed by the
implementer, separately from public document assertions.

Single stream selection only. Complex layout, multiple/rectangular selections,
text dragging and cross-monitor DPI remain outside this ticket. Linux IME was
not installed or tested.
