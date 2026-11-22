# GTK status baselines and Find/Replace rows

On 2026-11-22, a user reported raised Latin status captions in the Simplified
and Traditional Chinese interfaces on Fedora KDE Plasma 44, and overlapping
"Replace with" / "Wrap around" controls in the English Replace dialog.

Both symptoms reproduce in actual wxWidgets 3.3.3 GTK windows on the local
Ubuntu environment. This is independent evidence of the same layout defects;
it is not a Fedora validation run.

## Reproduction

`native_ui_layout` creates the production `EditorFrame`, loads each deployed
English/Chinese catalog and exercises its real controls. The registered tests
run through the private native sessions documented in `docs/ci.md`:

```text
python3 .github/scripts/ci.py test --backend x11 --cmake-dir <native-build> --tests '^(native_replace_layout_|native_status_baseline_)' --report-dir <fresh-report-directory>
```

Before either product fix, the English status baseline passes; both Chinese
baselines and all three Replace layouts fail. In the default local font:

| Observation | Before |
| --- | --- |
| Common final glyph in Chinese line field | Bottom at row 16 |
| Same glyph in UTF-8 field | Bottom at row 15 |
| English replacement label | Rows 215–231 |
| English Wrap around control | Rows 215–235 |

The status test appends the same Latin probe glyph to real translated captions,
then reads the actual Cairo surface after GTK's production draw. It compares
that glyph's painted row, rather than comparing the different ink bounds of
Chinese and Latin characters. The Replace test alternates actual Find/Replace
menu commands and asserts that the visible label, input and checkbox occupy
separate ordered rows inside the dialog. Each failure returns a nonzero process
exit code. Original tests, deadlines and private-session concurrency remain.

## Causes and changes

wxStatusBarGeneric centers every caption with a common character height, but
wxGTK draws a Pango layout from its top. CJK fallback changes the distance from
that top to the baseline: local mixed captions measure height/descent 19/5,
while Latin captions measure 17/4. Pango also applies a fractional baseline
shift between CJK and alphabetic font runs. A recorded 200% case has a line
baseline of 14 and a Latin run baseline of 14.205078; rounding those measurements
to integer logical pixels still leaves a visible physical-pixel difference.

The GTK status subclass measures the actual displayed captions, including
native ellipsizing and the grip reservation, and aligns their alphabetic
baselines within one shared text line. A measurement-only Latin anchor captures
Pango's script-dependent font-run adjustment; it is never displayed. Drawing
uses a fractional Cairo translation clipped to the original field, preserving
the physical-pixel alignment at native scale 2. Borders, text, ellipsis flags
and tooltips remain delegated to wxStatusBarGeneric. This does not select a
replacement font or encode a Chinese-specific pixel correction.
The Windows native status implementation is unchanged.

Find/Replace toggles control visibility then calls `Fit()`. Because the existing
minimum size can leave the dialog at its current size, no resize-driven layout
occurs. Explicit `Layout()` after the visibility changes restores the ordered
rows. Applying this change alone fixes all three Replace tests while leaving
the Chinese baseline tests red.

## Regression coverage

Replacement geometry is checked for all three UI languages on Windows and
Linux. Linux additionally checks the painted status baseline at default scale
and native scale 2 with font factors 0.75 and 1 (150% and 200% physical text
scale). Each case runs independently under X11 and Wayland in CI. The glyph
comparison derives contrast from the widget's foreground/background colors.
Existing packaged localization, status, shell and search tests cover the
unchanged behavior alongside these focused checks.

Local evidence is retained under
`F:/wxnotepad-evidence/gtk-localized-layout-2026-11-22/`, including the original
red reports (`x11-red-02`), the independent Replace-only correction
(`replace-fix-01`), font-metric instrumentation (`metrics-01`) and final
backend results. The diagnostic-only logging was removed before validation.

## Windows CI language-verification follow-up

The first hosted Windows run passed 272 of 274 tests; only the two new Chinese
replacement-layout cases failed before reaching their geometry assertions. The
existing packaged application passed both Chinese language scenarios on the
same runner, and its catalogs were deployed beside the test executable.

The new fixture implicitly converted the UTF-8 source literal `行` to `wxString`
using the Windows default converter. A native control using Windows-1252
reproduces the hosted failure: the actual caption remains `行 1 / 1`, but the
expected prefix becomes `è¡Œ`. On this development host, the default conversion
instead returns an empty prefix, which `StartsWith()` accepts and consequently
masked the defective comparison. Changing the C runtime character locale did
not reproduce the hosted failure: wxWidgets' Windows default converter uses
`CP_ACP` rather than the C runtime locale.

The fixture now constructs its expected prefix explicitly with `FromUTF8()`
and also requires it to be nonempty. The original language, row-order and
inside-dialog checks remain. Its CMake target explicitly depends on deployed
catalogs, including when built independently. Native control patches, hosted
logs and before/after reports are retained in `ci-followup/` within the evidence
directory above.

The same follow-up separates Pango field metrics from line aggregation and
glyph-pixel scanning from the baseline comparison. Owning native capture
objects are noncopyable because GTK callbacks retain their addresses. Native
windows retain temporary ownership until their explicit frame/application
handoff. These changes preserve the existing font, drawing and test rules.
