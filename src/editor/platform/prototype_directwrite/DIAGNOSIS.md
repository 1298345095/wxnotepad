# DirectWrite memory/resize diagnosis

This investigation starts from `3004f35`, retaining the successful 24-process
comparison and both GDI/DirectWrite branches. No production rendering behavior,
core parallelism, original assertion or timeout is changed by instrumentation.

## Feedback and hypotheses

The prior controlled results reproduce warm long-line resize regression in all
three repeats: default-cap medians 581.78 ms GDI versus 1258.24 ms DirectWrite
for 12 resizes. Blank-window private memory is 10.81 versus 48.29 MiB. These are
the existing red observations; no new product performance budget is invented.

Ranked explanations to test, not conclusions:

1. Graphics runtime/driver/target initialization explains much of blank-window
   memory. Predict a large committed-private jump before loading any document,
   localized to factory or HWND target construction.
2. Creating native text layouts while painting dominates warm resize cost even
   when the editor's character-position cache is warm. Predict high draw/layout
   time and call counts, with few worker measurement calls in that phase.
3. Target resizing/presentation or wx's eager wrapping/full repaint scheduling
   dominates. Predict target resize/EndDraw or core resize/wrap scopes account
   for the observed delay instead of text-layout creation.

Only after locating a hotspot is the corresponding original path compared.
Any resulting fix must preserve original text metrics, editing behavior,
parallelism and existing regression assertions, then rerun the original probe.

## Probe

`prepare_diagnostics.py` creates instrumented copies in the build directory,
separate from normal host/application objects. `diagnostic_metrics.h` counts
calls, worker calls and summed elapsed nanoseconds. Nested scopes overlap and
worker sums can exceed wall time; they must not be added as disjoint categories.

Memory checkpoints surround native graphics loading, factories, HWND target
creation, wrapping, resizes, New Document and actual pending-window deletion.
Module names identify newly loaded components, not their heap ownership.
VirtualQuery committed MEM_PRIVATE/MEM_IMAGE/MEM_MAPPED totals differ from
process private usage and working set; do not treat image size or mapped virtual
commit as resident private bytes.

The profiling-only target performs 60 natural resizes for sampling density.
The original 12-resize comparison and two 80-resize regressions are unchanged.
`directwrite_diagnostic 0 geometry` performs only an empty-window geometry
check, with timing/counter/memory sampling disabled, using the existing guard.

`directwrite_native_memory` is a targeted control for the observed initialization
jump. It reuses the original native factory/font/surface implementation and the
original ScintillaWin HWND render-target properties on a plain Win32 window,
without a wx window, editor model or layout scheduler. It controls that API
stage; it is not a measurement of the complete original Notepad4 application.

## WPT

Installed tools: `D:/Windows Kits/10/Windows Performance Toolkit`; native PDBs
are produced by the existing Release `/Zi` build. Windows kernel ETW needs an
administrator collector. The controller and test program run as standard user;
only `collect-wpt.ps1` elevates. User readiness and UAC confirmation are required
before running `run-diagnostics.ps1 -OutputDirectory <fresh Windows folder>`.

Profiles: CPU, GPU, VirtualAllocation and a private phase marker provider. The
collector uses its own unique instance, never cancels an unrelated recording,
waits at most 90 seconds for the controller and always attempts to stop/save.
The two owned test processes share a 60-second budget, and any still-running
owned child is stopped before reporting controller failure. On stop failure, only its named
collectors are stopped; raw ETLs are copied before cancelling that WPR instance.
Normal completion and all failures retain stdout, stderr and status JSON.

The controller saves the exact exe, PDB, generated sources, metrics header and
compile commands before launching the probe. Profiling overhead means these
instrumented runs establish attribution, not a replacement throughput claim.
The native memory control and long-line probe run serially in the same capture.

## Evidence validity

Evidence root: `E:/NewNotepad4/wxnotepad-evidence/directwrite-diagnosis-2026-11-14/`.
First capture `wpt-01` failed its initial geometry check while the probe was
elevated. Its custom provider GUID had braces, which WPR accepted syntactically
but omitted from its provider list, causing stop/merge failure. Its loggers were
stopped and raw ETLs recovered before cancellation. It is not valid long-line
resize evidence; initialization memory checkpoints are retained as observations.

One attempted geometry smoke used a stale generated probe and produced an
uncontrolled multiline run; its timing data is excluded. CMake now tracks all
generator inputs, and the corrected geometry-only check passes. WPR's provider
listing now shows the marker GUID after its braces were removed. The successful
second capture keeps the probe at the original standard-user integrity level.

## Successful WPT observations

`wpt-02/diagnosis.etl` completed both probes and saved successfully, with zero
lost events and buffers. Exact executables, PDBs, generated sources and compile
commands are retained in `wpt-02/binary-record/`. Original failed recordings
remain preserved separately.

Private commit at HWND target creation rises from 5.07 to 36.11 MiB in the
native control, and from 11.12 to 42.06 MiB in the wx probe. The roughly 31 MiB
increase is therefore reproduced without wx or its editor scheduler. Graphics
driver modules load at this boundary; module snapshots alone do not establish
allocation ownership. First-painted blank windows are 49.02 and 48.28 MiB,
respectively. This is a control of the graphics API path, not a benchmark of
the full original Notepad4 application.

The 60 instrumented warm long-line resizes take 6733.73 ms wall and 6046.88 ms
process CPU. Nested scope totals are:

| Scope | Calls | Summed elapsed ms |
| --- | ---: | ---: |
| Core paint | 134 | 4944.19 |
| DrawTextCommon | 12408 | 3684.10 |
| CreateTextLayout | 12542 | 17.41 |
| Core resize | 60 | 311.23 |
| Core WrapAll | 60 | 179.29 |
| EndDraw | 134 | 125.07 |
| Pixmap creation | 360 | 41.32 |
| Target resize | 60 | 23.80 |
| MeasureWidths | 0 | 0.00 |

Warm measurement caching is effective. Creating the text-layout object itself
does not dominate; the broader drawing scope includes lazy layout work and
native drawing. These instrumented timings identify the area to inspect and
are not replacement throughput results. The all-event stack report mixes CPU,
graphics and allocation events; its hit percentages are not CPU percentages.

The subsequent `Sampled Profile`-only export filters the owned probe PID and
exact warm marker interval. Of 5813 samples, `DrawTextLayout` has 3535 inclusive
hits (60.81%); its color-glyph path reaches `TranslateColorGlyphRun` with 3120
hits (53.67%). `GetPaletteEntries` has 1818 exclusive hits (31.27%). This fixture
contains emoji, and the original backend enables color fonts too. MacType is
on the drawing call chain, but its presence and inclusive hits do not establish
its incremental cost. Some stacks have incomplete roots, and tracing itself
adds kernel work; these sample shares are attribution within this capture,
not an uninstrumented timing prediction. See `warm-sampled-stacks.html`, its
extracted JSON and `warm-sampled-summary.txt`.

## Original buffering contract

Comparing that drawing path found a concrete host omission: pinned
`ScintillaWin.cxx`, `SetTechnology`, sets `view.bufferedDraw` to false for
DirectWrite, whose render target already supplies buffering. The prototype
selected the technology but retained the default extra Scintilla line pixmap.

The native oracle now checks technology and buffering on an actual EditorFrame.
The buffering assertion failed before the fix (`buffer-contract-red.log`).
Setting `SetBufferedDraw(0)` only in DirectWrite prototype initialization makes
it pass. All seven existing checks pass, including unchanged exact original
metric/raster comparisons, worker loops, both 80-resize loops and timeouts.
Canonical clang-format and native clang-tidy also pass their existing checks.

Matched uninstrumented binaries are saved in `buffer-comparison/`, with the
buffer state explicitly observed alongside unchanged geometry assertions.
The GDI branch and Linux backend are unchanged.

## Controlled buffering A/B result

After a fresh user-ready response, all 24 original-probe processes completed:
three interleaved repeats, both fixtures, diagnostic cap one and unchanged
default cap 20. No profiler or offline export ran during the measurement.
Both binaries include the same buffer-state observation. Every fixture's
geometry and CHECK records match across variants, repetitions and caps, while
the expected `buffered_draw` value changes from one to zero. Full text and
save-point assertions remain unchanged and pass.

At default cap, warm long-line resizing changes from 1142.11 to 1116.29 ms
median (2.3% lower); the three observations span 1127.56–1209.01 versus
1046.89–1124.45 ms. The small median change does not establish a general speedup
from only three repeats or resolve the prior GDI comparison's warm regression.
The cap-one control changes from 1241.10 to 995.38 ms; this is a diagnostic
result, not a reason to change the application's concurrency.

The memory result is similarly limited: blank long-line windows change from
48.26 to 47.78 MiB, while after New Document they are 81.57 versus 81.84 MiB.
Removing the extra line buffer does not remove the graphics initialization or
retained-memory costs. Keep the original-behavior fix, but do not present it as
the main performance fix. Detailed medians for both fixtures/caps are in
[RESULTS.md](RESULTS.md); raw results, matched binaries, sources and the validating
summarizer are in `buffer-comparison/` under the evidence root.

The original-application comparison exposed a second concrete host difference before
timing: wx's resize handler eagerly wrapped and pulled the viewport to the
caret, whereas the original SizeWindow path preserves it. The prototype-only
correction passes the exact paired viewport/scroll oracle and all seven existing
checks. See [ORIGINAL_COMPARISON.md](ORIGINAL_COMPARISON.md) for the common
external driver, original-app normalization and retained red/green evidence.
After fresh readiness, three interleaved pairs complete with identical scenes,
all 13 viewport/scroll observations and whole styled-document hashes. Original
DirectWrite takes 1256.56 ms median and corrected wx takes 929.38 ms for 12 warm
resizes under the common external timing boundary. Thus this scene does not
reproduce a wx-specific slowdown; the original also pays warm native drawing
cost. It is not a same-boundary comparison with the earlier GDI results or an
isolated speed measurement of the viewport correction. The controlled window
has ended; any further capture needs fresh readiness.
