# Original word, line and document-edge selection

Issue: [#213](https://github.com/1298345095/wxnotepad/issues/213).
The reference is Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`:
`EditSelectWord`, `EditSelectLines`, and the selection command handlers in
`Notepad4.cpp` and accelerators in `Notepad4.rc`.

Select All is in Edit before Swap (Ctrl+A). Search exposes Select Word
(Ctrl+Alt+Space) and Select Lines (Expand Selection) (Ctrl+Shift+Space)
before selection of lines in the current block. Select to Document Start
and Select to Document End precede Select to Next/Previous and have no
accelerators.

Select Word uses the pinned core's word classification. An empty selection
first selects the current word, then tries the next word when starting in
whitespace. If neither produces a selection, or a selection already exists,
it selects from the current line's indentation boundary to its logical end.
Select Lines shares the original algorithm with current-block selection.
The Visual Studio preference bit selects the core's line-selection mode;
other preferences expand stream endpoints to logical line starts, including
the following line. Repeated invocation follows the original endpoint rule.

Document Start sends SetSelectionStart(0), and Document End sends
SetSelectionEnd(length). These original core messages determine which
boundary changes, including reverse selections and rectangular selections.
They do not run a caret-extension command or add scrolling absent from the
original handlers.

`tests/reference_capture/capture_selection_commands.py` uses the owned
original application on a private Windows desktop without clipboard access:

```text
python -X utf8 tests/reference_capture/capture_selection_commands.py ORIGINAL_EXE EVIDENCE_DIRECTORY tests/selection_commands_reference.h
```

The committed native reference samples cover word boundaries, indentation,
trailing whitespace, blank lines, Unicode, CRLF/LF, a final line without EOL,
forward/reverse selections, all four line-selection preferences, repeated
line expansion, rectangles with virtual space, multiple selections,
read-only content, wrapping and folded content. The capture records the
original executable hash and exact public editor state in external JSON.

The registered `original_selection_commands` fixture invokes native menu
entries and the original keyboard gestures through the shared native-key
protocol on Windows, private X11 and private Wayland. It compares directed
endpoints, mode, count, virtual space and folded visibility to the original
samples and checks document bytes, current encoding, save point and usable
Undo/Redo history. Existing current-block/bookmark native fixtures remain
registered as regressions for the shared selection machinery.

Retained rectangular subranges use the core's native font measurements:
`SetRectangularRange` maps x coordinates back to byte positions. The original
reference process selects Microsoft YaHei UI at 9 points on this host, while
the native controls initially use monospace/Consolas at 12 points. For the
first document's rectangle (anchor 4, caret 13, virtual spaces 3/2), the
original prepared subrange is 6/15 and wxGTK's is 7/15. Outer rectangle
endpoints, direction, mode, count and virtual spaces are identical.

The independent `--verify-rectangles` capture checks 36 original before/after
samples, including both document edges and repeated promotion to Lines in
both Visual Studio preferences. Document-edge setters retain every subrange;
promotion to Lines adjusts only the main range and retains every non-main
subrange. The native test checks exact before/after equality for retained rectangles
at document edges. Rectangle-to-lines cases seed the independent original
before byte ranges through public Scintilla selection messages, so fonts do
not change the main range's initial direction. Their complete resulting
ranges and direction, every stream and explicitly added multiple range,
and all outer geometry compare to literal original samples. This follows the editing-behavior policy in
ADR 0002 without requiring matching font pixels.

```text
python -X utf8 tests/reference_capture/capture_selection_commands.py ORIGINAL_EXE EVIDENCE_DIRECTORY tests/selection_commands_reference.h --verify-rectangles
```
