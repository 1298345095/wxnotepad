# Original-application DirectWrite comparison

Question: is the warm long-line drawing cost common to the original native
renderer, or is the wx host doing additional work? This follows the WPT and
buffer-only experiments in [DIAGNOSIS.md](DIAGNOSIS.md).

## Contract and controller

Use the existing x64 Notepad4 reference executable from pinned `89b8b36`, with
its own temporary portable INI, and `directwrite_comparison.exe`. The latter
uses the same EditorFrame, document loading, host core, baseline feature
libraries and native DirectWrite adapter as this prototype. A generated copy
of editor_control.cpp forwards public Scintilla HWND messages for the test
driver only. Normal applications and correctness targets have no such bridge.

`compare_original.py` drives both independent processes from outside through
the same native frame resize, followed by two synchronous invalidating paints.
It uses physical coordinates, checks actual client dimensions, and queries the
wx core's existing internal wrap-width oracle outside measured intervals.
The original application remains an independent, unmodified executable; its
fonts, styles, layout cache, paint phases and margins are normalized to the wx
scene through public messages, then read back exactly. This is a controlled
rendering comparison, not a comparison of default application preferences.

The complete existing 1 MiB UTF-8 TOML long-line fixture contains Chinese and
emoji (1,048,597 bytes). Initial client viewport: 992 × 598 physical pixels.
Twelve resizes alternate 872 × 598 and 1172 × 598. At 150% scaling these widths
match the earlier 600/800 DIP probe's actual client widths. The initial setup
can adjust the native frame up to three times while nonclient/statusbar layout
settles. Each measured resize performs exactly one frame resize and two paints;
it fails rather than correcting a mismatched measured viewport.

Per-step client dimensions, DPI, margins, line height, wrap count, first visible
line, screen-line count and selection must agree across applications/repeats.
Both use technology DirectWrite and no extra Scintilla line buffer. Complete
text and styles are compared byte for byte and save points remain unchanged.
The driver respects the pinned GetStyledTextFull layout: separate style/text
arrays, rather than upstream Scintilla's interleaved representation.

Default mode performs no timing or memory sampling. `--measure --scene <validated
scene.json>` runs three interleaved repeats (six processes), retaining each
resize's wall/process CPU time. The result is the sum of the 12 resize/paint
intervals. Geometry queries are outside those intervals for both applications.
This differs from the earlier in-process timing boundary; compare only these
new paired variants with each other. It does not measure compositor presentation
latency or fixed-work/full-pending-layout throughput. Production concurrency and
the existing 80-resize tests, numeric/raster assertions and timeouts are unchanged.

Private/working-set snapshots occur after quiet seconds outside timing. Both
processes retain an identical 4 MiB remote scratch allocation for full styled
document checks; absolute memory values include it. Original startup font/cache
history and application-shell differences also remain part of process memory.

## Red/green observation

`smoke-06/results.json` exposes a real host difference: after each narrowing,
the original preserves the prior viewport, while wx pulls the viewport to the
caret. Font/style data, physical sizes, line height, natural wrap counts and
the complete styled-document hash already agree. The script rejects the
remaining first-visible-line mismatch, so no timing comparison is accepted.

The original ScintillaWin::SizeWindow calls ChangeSize and lets normal painting
perform wrapping. The wx Core::Resize additionally called UpdateWrapLayout,
which eagerly invokes WrapLines(wsAll) and EnsureCaretVisible. The DirectWrite
prototype now follows the original path. GDI/Pango keep their prior path and
the preserved GDI branch is unchanged. `smoke-07` validates the corrected paired
scene, and the unchanged seven correctness checks pass. This establishes the
behavioral correction; its performance effect has not yet been measured.

Evidence root:
`E:/NewNotepad4/wxnotepad-evidence/original-directwrite-comparison-2026-11-14/`.
Failed preparation runs and the exact before-fix binary/PDB/sources are retained.

## Controlled result

After fresh user readiness, `measure-01` completed one pair, then rejected the
second original process before timing: the original app had a queued automatic
line-number update after font changes, replacing the controlled width 25 with
40. Its partial results are retained and excluded from the accepted comparison.
Setup now realizes fonts and allows queued UI work to run before applying the
controlled margins. No timed resize or assertion was relaxed.

`measure-02` completes all three interleaved pairs with exact scene, all 13
geometry/scroll records and entire styled-document hashes equal. Each pair uses
the same default parallel implementation; no thread cap was changed. No builds,
analyzers or profiler/export processes ran concurrently. The whole controlled
window, including the rejected preparation and retry, ended within four minutes.

Twelve warm resizes, milliseconds (three observations and median):

| Application | Wall observations | Wall median | Process CPU median |
| --- | --- | ---: | ---: |
| Original Notepad4 / DirectWrite | 1256.56, 1287.98, 1244.49 | 1256.56 | 1203.12 |
| wx DirectWrite / corrected resize | 941.30, 892.36, 929.38 | 929.38 | 828.12 |

On this machine and fixture, wx wall median is 26.0% lower. The original
application also incurs substantial warm DirectWrite drawing cost. This paired
scene does not reproduce a wx-specific slowdown; it does not establish a
general speedup for other documents or quantify the viewport fix's isolated
speed effect. Before-fix wx draws different viewports, so it is not accepted
as an equal-scene timing control. The earlier GDI numbers use another timing
boundary and are not combined with these results.

Median private memory, MiB, including the equal 4 MiB test scratch allocations:

| Application | Wrapped | After resizes |
| --- | ---: | ---: |
| Original Notepad4 | 109.85 | 110.76 |
| wx DirectWrite | 83.77 | 83.89 |

These are observed process totals under normalized editor settings, including
original startup fonts/caches and different application shells. They do not
isolate framework allocation overhead or establish a general memory advantage.
The separate raw native initialization control remains the evidence for the
roughly 31 MiB graphics-target initialization stage.

All seven original correctness checks, canonical clang-format (360 tracked
files), maintained-source native clang-tidy and explicit generated-bridge tidy
checks pass. Separate Spec/Standards source reviews pass; the one native-flag
naming finding was corrected. The performance window is closed. Any further
capture requires fresh readiness.

## Run

Build `directwrite_comparison` with the standalone CMake project described in
[README.md](README.md). From the repository root in Git Bash, substitute fresh
output paths and the existing pinned reference executable:

```sh
python -X utf8 src/editor/platform/prototype_directwrite/compare_original.py --original <Notepad4.exe> --candidate .ci/directwrite-prototype/directwrite_comparison.exe --core-root <prepared-original-core> --output <fresh-smoke-folder>
```

After the user closes high-load applications and confirms readiness:

```sh
python -X utf8 src/editor/platform/prototype_directwrite/compare_original.py --original <Notepad4.exe> --candidate .ci/directwrite-prototype/directwrite_comparison.exe --core-root <prepared-original-core> --output <fresh-measurement-folder> --measure --scene <smoke-folder>/scene.json
```
