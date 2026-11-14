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

Keep both prototype branches. Investigate the blank-window graphics cost and
the warm long-line rendering/resize cost before deciding on production adoption.
Their internal causes have not been isolated by this experiment. No application
memory budget has been agreed, and no production merge is made here.

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

Performance is recorded after the final code changes. No further timing run is
authorized by the completed window. Raw logs, binary hashes, executables, images,
reports and the summarizer remain in the Windows evidence directory listed in
[README.md](README.md). Linux's Pango/Cairo code is unchanged by this experiment.
