# Word wrap settings and native reference

Implements [issue #211](https://github.com/1298345095/wxnotepad/issues/211) against
Notepad4 `89b8b36e0ee2c23f73be922315f17261ccfbbe1a`.

The host retains the original nonzero preferred mode when wrapping is disabled.
The settings dialog selects that preferred mode even when the current wrap toggle
is off, matching `WordWrapSettingsDlgProc`. Imported modes clamp to Word through
Auto; continuation-indent indices clamp to 0–7. The original default is Auto.

Continuation indentation uses the original core modes and
`EditSetWrapIndentMode`/`EditSetWrapStartIndent` mapping. Fixed level indentation
uses indent width, falling back to tab width when indent width is zero. Language,
tab settings and file variables reapply it through the existing indentation path.
`truncate-lines` overrides only the loaded document's wrap enablement; saving or
creating a new document retains the global preference.

## Approved immediate-highlight improvement

The original dialog updates `bHighlightCurrentSubLine`, but its parent command
does not send `SCI_SETCARETLINEHIGHLIGHTSUBLINE`. This was independently observed
at both 144 and 96 DPI after actual native checkbox changes, completion of the
parent command and native painting:

| Action | Checkbox before → after | Parent wrap mode before → after | Active core subline flag before → after |
| --- | --- | --- | --- |
| Uncheck and confirm | 1 → 0 | Auto (4) → Character (2) | 1 → 1 |
| Reopen, check and confirm | 0 → 1 | Character (2) → Whitespace (3) | 1 → 1 |

The user explicitly approved applying this setting immediately as an improvement
over that delayed original behavior. wxnotepad sends the existing core highlight
message on OK. Cancel changes no preference or editor state. The independent
wrapping comparisons retain the checked highlight setting on both hosts so the
approved difference is explicit rather than hidden in a geometry comparison.

## Independent original executable comparison

`tests/reference_capture/capture_word_wrap.py` launches an owned portable copy of
the actual original executable on a private Win32 desktop. It waits for startup
input idle, uses an owning-thread message barrier, verifies all native dialog
controls before operating them, and checks the intended document bytes before
sampling. It uses native original dialog choices rather than substituting direct
core wrap-setting calls.

The executable used for the November 17, 2026 capture has SHA-256
`1425c9a4b5ab2ea1fa3ba088f38dd987b763b7b8feff23dde6243f7855e86a78`.
Its provenance is the pinned original used by the existing encoding and native
rendering reference captures. Raw metadata, logs, copied portable hosts and
highlight observations are retained outside the repository under
`F:/wxnotepad-evidence/native-editor-controls-2026-11-17/211/`.

The reviewed inputs are `tests/data/word_wrap_inputs.tsv`: mixed spaces/tabs and
mixed CRLF/LF/CR endings, CJK/emoji text, an unbroken long line, and a folded
sample with a wrapped header. The original tables contain 32 observations per
DPI: each sample in Word, Character, Whitespace and Auto modes at 480- and
720-pixel reported native client widths. All eight continuation-indent choices
are represented. Fold levels are explicit to isolate wrapping from lexer changes.

The original and wx host both use GDI, plain Consolas at 10 points, default font
quality/charset/stretch, 100% zoom, tab width 8, indent width 3, one-pixel edge
margins and zero margin-column widths. The comparison checks the exact native
client viewport, actual window DPI, wrap mode/indent/symbol flags, subline
selection/highlighting flags, row count, fold visibility, reversed selection,
modified/Undo/Redo state, line height, Latin/CJK glyph advances and every input
byte. All 64 independent comparisons passed.

Cross-process geometry must use the target's DPI context:
[GetWindowRect is DPI-virtualized](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect),
and [SetThreadDpiAwarenessContext](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setthreaddpiawarenesscontext)
sets the caller's context and returns the prior context for restoration. Python's default
unaware context virtualized a requested 480-pixel viewport to 720 physical pixels
for a 144-DPI PMv2 original window. The capture now explicitly uses PMv2 at 144
DPI and the unaware context for the complementary 96-DPI target. It asserts the
actual client dimensions and font attributes; the failed premise and earlier
font-style-index discovery are preserved in the external evidence. No cases,
expected values or geometry assertions were reduced to accommodate either error.

## Native DPI validation and limits

The normal Windows fixture ran at the monitor's actual 144 DPI. Its complementary
96-DPI fixture uses the same unaware-thread emulation as the existing caret and
current-line tests. Both execute the complete native settings/persistence case,
checking the actual dialog DPI against Win32, equality with the parent DPI,
visibility of every native control/button within the dialog, and fit within the
native work area. Both passed, including actual choices, Cancel/OK, shortcuts,
margin clicks and independent process restart.

The independent original table comparison is Windows/GDI-specific. It does not
assert pixel identity between platforms or a physical monitor transition. At 96
DPI, reported native coordinates are virtualized by the emulated target; system
monitor scaling is unchanged. X11 and Wayland retain their own native behavior,
resize and persistence fixtures and deadlines.

To recapture using the existing installed original executable:

```text
python tests/reference_capture/capture_word_wrap.py ORIGINAL_EXE tests/data/word_wrap_inputs.tsv FRESH_OUTPUT
python tests/reference_capture/capture_word_wrap.py ORIGINAL_EXE tests/data/word_wrap_inputs.tsv FRESH_OUTPUT_96 --dpi96
```

Raw captures belong outside the repository. Review their numeric rows before
updating the committed `word_wrap_original_144.tsv` and `word_wrap_original_96.tsv`
tables. Run native capture/probes through the entry points in `docs/ci.md`.

## Approved same-host CI layout reference

Windows CI measured the two Chinese glyphs at 24 pixels at 96 DPI, while the
development host's frozen original table measures 28. The user approved obtaining
font metrics and wrapped-row counts from an independent original control on the
same CI machine, preserving exact comparisons and the original frozen tables.

The existing independently built, untouched pinned Win32 core replays all 32
scenes at current DPI and in complementary 96-DPI emulation. It reads the frozen
original dialog settings as scene inputs. Configuration, fold visibility,
selection and history must still equal their frozen values; only the wrapped-row
count, line height and Latin/CJK widths are measured anew. It verifies every
input byte and exact client geometry. wxnotepad then compares all seventeen
observed fields exactly against these independent records.

The source archive retains its existing pinned hash check. Both original tables
and inputs remain in CI evidence. `WXNOTEPAD_ORIGINAL_WRAP_REFERENCE` selects the
generated record directories; standalone runs without it keep using the frozen
tables. This changes the layout reference source, with no changes to assertions,
deadlines, parallelism or application font/rendering behavior.
