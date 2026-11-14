# DirectWrite prototype results

Date: 2026-11-14. Implementation: `2941dea`, with original backend integration
in `9be6c22`. GDI control: the independent-worker prototype from `efc1366`.
The GDI branch `codex/prototype-worker-measurement` remains unchanged.

## Verdict

The original Direct2D/DirectWrite implementation works through the wx host with
matching original numeric/raster results, parallel measurements and correct
Microsoft Pinyin behavior. This validates integration feasibility. It does not
establish DirectWrite as the default backend: initial wrapping and multiline
resize interactions improve substantially, but private memory increases and
warm long-line resizing regresses.

Keep both prototype branches. The follow-up below localizes much of blank-window
memory to native graphics initialization and warm time to drawing, including
color glyphs. It restores a missing original buffering setting, but does not
resolve the warm long-line regression. No application memory budget has been
agreed, and no production merge is made here. The later
[original-application comparison](ORIGINAL_COMPARISON.md) also corrects resize
viewport behavior and finds 1256.56 ms original versus 929.38 ms wx for 12 warm
resizes in the same scene. It does not reproduce a wx-specific slowdown, while
the earlier GDI comparison has a different timing boundary and remains separate.

## Controlled Windows comparison

The user closed high-load applications before this run and was notified when
the window ended. MSVC 2026 Release, wxWidgets 3.3.3, the same baseline libraries,
nominal fonts, 150% display scaling, 1 MiB TOML fixtures and window sizes. Three
independent repeats interleave GDI and DirectWrite; diagnostic caps are one and
the unchanged default of 20. Total: 24 independent processes, all successful.

The DirectWrite probe reuses the prior harness: wrapping plus going to the end
and two completed paint handlers, followed by 12 natural alternating resizes.
Memory checkpoints occur outside timed intervals after one quiet second. The
normal application is built with a separate host object and has no cap hook.

Default-cap medians, milliseconds:

| Interaction | GDI worker prototype | DirectWrite | Observation |
| --- | ---: | ---: | --- |
| Multiline initial wrap + paints | 697.26 | 205.48 | 3.39× faster |
| Multiline 12 resizes | 7489.25 | 2044.91 | 3.66× faster |
| Long-line initial wrap + paints | 1805.01 | 551.95 | 3.27× faster |
| Warm long-line 12 resizes | 581.78 | 1258.24 | 2.16× slower |

At cap one, corresponding GDI → DirectWrite values are 861.65 → 378.24,
3903.34 → 3367.23, 2138.45 → 899.52 and 551.99 → 1291.90 ms.

During default-cap multiline work, CPU/wall ratios are roughly 3.14 → 10.21
for the initial phase and 2.71 → 11.74 for resizes. Median process CPU time for
multiline resizing rises from 20.08 to 23.36 seconds. Configured cap 20 is not
20 simultaneously active workers, and shorter wall time is not lower CPU cost.

These are interaction/paint-handler observations, not compositor presentation
latency or fixed-work/full-pending-layout throughput. The core's adaptive budget
can change how much work is completed during the same interaction sequence.
Measurement-detail counters inherited from the previous research header are
zero in these uninstrumented binaries; only wall/process CPU counters apply.

## Memory and handles

Median process private usage for the multiline fixture at default cap, MiB:

| Stage | GDI | DirectWrite | Difference |
| --- | ---: | ---: | ---: |
| Blank window | 10.81 | 48.29 | +37.48 |
| After initial wrap | 19.84 | 65.87 | +46.03 |
| After resizes | 20.87 | 68.81 | +47.94 |
| After New Document | 18.91 | 69.74 | +50.83 |

After the long-line fixture and New Document, private usage is 27.44 → 81.82 MiB.
The measured Windows graphics stack and caches are part of these process costs;
the experiment does not attribute the entire increase to any one cache or API,
and process private usage does not account for all GPU/system memory.
Multiline GDI object counts after resizes fall from 168 to 69, and after New
Document from 147 to 69. Fewer GDI objects does not imply lower total memory.

## Geometry and renderer-specific results

Every process records 14 geometry observations and enforces core wrap width
against the client width minus text start and right margin. CHECK records and
all geometry observations are stable within each renderer across repetitions
and diagnostic caps. The cross-renderer client viewports match.

Multiline automatic line-number margins differ by one physical pixel:
GDI text start 104, DirectWrite 105. At initial width, wrap widths are 887 and
886; at the last width, 1067 and 1066. Both final CHECK records report 1,048,582
bytes, 10,713 lines and first/last wrap count two. Thus the effective text areas
are not pixel-identical despite equal client viewports; results describe each
backend's native behavior, not an isolated same-metrics throughput comparison.

Long-line geometry matches exactly. CHECK records both report 1,048,597 bytes
and two logical lines, while native wrap counts are 11,512 for GDI and 11,523 for
DirectWrite. Each is stable and the DirectWrite metrics match its original
backend. No GDI width rounding was imposed on DirectWrite to force equality.
Full text and save-point assertions passed in every process.

## Correctness and review

- 324 native DirectWrite numeric records match exactly, including byte positions.
  Four workers × 64 iterations per group preserve those original serial values.
- Actual HWND DPI agrees with the adapter. Em sizes corresponding to 96/144/192
  DPI cover three point sizes and normal/bold/italic styles; these are not three
  physical monitor configurations.
- Direct and buffered rasters each exactly match their original counterpart.
  Original native direct/buffered paths differ by 225 color channels; the initial
  failed assumption of three-way equality and all output images are retained.
- Seven final integration checks pass: original wrapped resize, metric/raster
  oracle, caret, caret at 96 DPI, auto insertion, auto completion and popup.
  The two 80-resize loops, assertions and original timeouts are unchanged.
- The user verified normal Microsoft Pinyin preedit size/baseline, candidates,
  commit/cancel/single Undo, wrapped preedit with line numbers, and text clarity.
- Spec review found two target-loss recovery defects; both were corrected by
  reusing full inherited graphics invalidation and handling resize loss before
  painting begins. Standards review requested named oracle cases; corrected.
  Both reviewers report zero remaining findings after `2941dea`.
- Native clang-tidy completed without compiler/analyzer errors; new actionable
  static initialization/vector allocation warnings were corrected. Canonical
  clang-format passes. Existing inherited warnings remain documented in logs.

The measured binaries were built from `2941dea`. Subsequent changes report native
target allocation failure through HRESULT and narrow the oracle enum storage;
neither changes the measured successful application path. The original measured
binaries/hashes are retained. A separate buffer-only comparison is reported
below. Raw logs, binary hashes, executables, images,
reports and the summarizer remain in the Windows evidence directory listed in
[README.md](README.md). Linux's Pango/Cairo code is unchanged by this experiment.

## Buffer-contract follow-up

Measured fix: `882f3e9`. The before binary retains the extra Scintilla line
pixmap buffer; the after binary disables it, following the original
ScintillaWin DirectWrite technology switch. Both share the same geometry and
buffer-state reporting, unchanged fixtures and 12-resize sequence, MSVC Release
settings and runtime DLLs. This compares two wx DirectWrite variants, not the
original application's total cost.

The user again closed high-load applications. Three repeats interleave both
variants at cap one and default cap 20: 24 processes, all successful. All 14
geometry records per process match within each fixture across variants and
caps after separating the intentionally changed buffer-state field. CHECK
records match, full text/save points are preserved, and reported thread caps
remain one and 20. No profiler/exporter ran concurrently. The sampling window
has ended; further performance runs require fresh readiness.

Median interaction times, milliseconds:

| Fixture / phase | Cap 1 before | Cap 1 after | Cap 20 before | Cap 20 after |
| --- | ---: | ---: | ---: | ---: |
| Multiline initial wrap + paints | 379.65 | 372.65 | 207.21 | 189.72 |
| Multiline 12 resizes | 3405.49 | 3414.99 | 2090.54 | 2069.67 |
| Long-line initial wrap + paints | 918.50 | 905.56 | 580.64 | 545.79 |
| Warm long-line 12 resizes | 1241.10 | 995.38 | 1142.11 | 1116.29 |

Default-cap warm long-line observations are 1209.01/1127.56/1142.11 before and
1046.89/1116.29/1124.45 after. Median wall time falls 2.3%; median process CPU
falls from 1078.12 to 937.50 ms. With only three repeats, the small default-cap
wall change does not establish a general speedup or resolve the earlier warm
GDI regression. The larger cap-one result does not justify reducing production
parallelism. Default-cap multiline 12-resize process CPU medians rise from
23421.88 to 25593.75 ms despite nearly unchanged wall time; these retain the
original interaction/adaptive-work interpretation, not fixed-work throughput.

Long-line private-memory medians at default cap, MiB:

| Stage | Before | After |
| --- | ---: | ---: |
| Blank window | 48.26 | 47.78 |
| After initial wrap | 76.59 | 75.43 |
| After resizes | 80.97 | 80.93 |
| After New Document | 81.57 | 81.84 |

The correction restores original behavior; it does not remove the large native
graphics cost or retained process memory. [DIAGNOSIS.md](DIAGNOSIS.md) records
the separate native initialization control and sampled color-glyph hotspot,
with attribution limits. The subsequent [original-application experiment](ORIGINAL_COMPARISON.md)
uses a common external resize/paint timing boundary and reports its results
separately from this buffer-only comparison.

Evidence: `E:/NewNotepad4/wxnotepad-evidence/directwrite-diagnosis-2026-11-14/buffer-comparison/`.
`run-01/results.json` and per-process logs retain all measurements;
`summarize.py` validates geometry, CHECK records, buffer state and thread caps
before printing `summary.log`. Binaries, PDBs, generated sources and hashes
identify both variants. Existing seven correctness checks, canonical format,
native tidy and separate Spec/Standards reviews passed for the source fix;
this follow-up changes only documentation.
