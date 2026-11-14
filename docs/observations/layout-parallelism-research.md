# Layout parallelism: source findings and controlled probes

Source review dated 2026-11-13. Baselines: Notepad4
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`, wxWidgets 3.3.3, and wxnotepad
`05b7d86abd9bb4c290cc342f94ab2c72a6a1b19d`. This note separates verified source
behavior from measured interaction costs and remaining performance hypotheses.
Production code and acceptance tests were not changed for this investigation.

## Answer

**Yes, original Notepad4 also locks text measurement.** Its GDI backend shares
one Surface HDC and serializes font selection plus the native extent call.
The restored core therefore never promised that GDI measurement itself would
scale with the number of layout workers. `NativeMutex` uses an exclusive Windows
SRW lock in these guards. Workers can still overlap conversion,
cache-independent layout work and work on different lines. The original
DirectWrite measurement path instead creates a separate text layout per call;
it has no corresponding application-level measurement mutex. [N1][N2][N10][N14]

Restoring worker dispatch establishes correct concurrent execution. It does not
establish a measurable improvement in startup, saving, or an entire resize loop.
The existing resource run used plain ASCII with wrapping disabled; its 800-resize
replay included window dispatch, painting and a 3 ms delay per iteration. Those
observations do not isolate the portion changed by parallel layout. This is a
limitation of the comparison, not evidence that the restored path does nothing.
See the [previous measurement protocol and observations](parallel-measurement-README.md).

A second comparison condition matters: original Notepad4 defaults to DirectWrite
(RTL forces GDI), whereas the current Windows wx adapter uses GDI and ignores
the Surface allocation technology parameter. Comparing both applications with
their defaults therefore also compares measurement engines. [N3][P1]

## Verified locking and ownership

| Path | Native ownership and synchronization | Work outside the measurement lock |
| --- | --- | --- |
| Original GDI | `SurfaceGDI::measureLock` is per Surface. `MeasureWidths` and `MeasureWidthsUTF8` lock `SetFont` and `GetTextExtentExPointW/A`. | Text conversion, temporary position allocation, UTF-16-to-byte mapping and final position filling. [N1] |
| Current Windows | Each `WxSurface` owns one `TextMeasurement`, containing its own compatible HDC and 1x1 target-format bitmap. All workers using that Surface share its `Data::mutex`. The lock includes font-handle lookup, selection, native extents, tab correction, font restoration and `GdiFlush`. A separate font mutex protects the DPI/height font cache. | UTF conversion and extent-vector allocation precede the lock; UTF-8 byte-position expansion follows it. The tab correction loop currently remains inside even when there are no tabs. [P1][P2] |
| Current GTK | Every measurement object owns its Pango context/layout. `PrivateFontMap` keeps a cache on the constructing GUI thread, keyed by source-map pointer, serial and resolution. Surfaces from that cache share one independent native `FontMapState` **and its mutex**. | UTF decoding/validation precedes the lock. `SetText`, extents, Unicode cursor attributes, and byte-position expansion all remain inside it. Context/layout destruction also takes the map mutex. [P3] |
| Original DirectWrite | `MeasurePositions` builds a local `TextLayout`, obtains cluster metrics, then maps local positions. `LayoutCreate` uses the shared DirectWrite factory and the font's existing text format. | There is no `measureLock` in this path; this does not prove that the operating system has no internal synchronization. [N2] |

The current locks are not simply optional overhead. Microsoft states that GDI
does not serialize shared objects, and requires `GdiFlush` when a thread releases
ownership of a GDI object protected by a mutex. Removing the flush or mutex would
violate the documented handoff rule; the previous precise test also observed
incorrect metrics before the flush fix. [A1][A2]

DirectWrite is not grounds for a blanket thread-safety claim. Microsoft's
`IDWriteTextFormat` reference explicitly says the object "may not be thread-safe"
and notes that it carries formatting state. The original code avoids sharing
the mutable per-text layout between calls, but still shares its font format and
factory. The inspected references do not establish a general guarantee for
arbitrary concurrent mutations of those objects. Drawing's Direct2D factory is
single-threaded in the original source; measurement uses DirectWrite rather than
drawing through that render target. The original advertises
`Supports::ThreadSafeMeasureWidths` for this backend; that is its implementation
contract, not proof of unrestricted API thread safety. [N2][N12][N13][A3][A4]

Pango's primary source documents that the default Cairo font map has been
per-thread since 1.32.6 so PangoCairo can be used safely from multiple threads.
That supports investigating independent per-worker native state. It does not
justify removing synchronization from the current deliberately shared map.
wxWidgets recommends keeping GUI calls on the GUI thread; the current snapshots
capture native data there and perform native measurement on workers. [A5][W3][P3]

## Verified dispatch and serial work

The pinned core uses a 4,096-byte layout block and a 250 ms idle wrapping budget.
Its minimum long-line parallel length is adaptive:
`max(4096, durationWrapOneUnit.ActionsInAllowedTime(0.25) / 128)`.
It is not a constant 4 KiB threshold for every layout call. [N4]

- `WrapBlockWorker::Start` requires at least two lines, at least two hardware
  threads and at least 4,096 bytes in the selected line range. Worker count is
  `min(bytes / 2048, line count, hardware concurrency)`. It submits every worker
  and waits for all of them. Workers fetch one line index atomically at a time,
  protect significant-line cache retrieval with `mutexRetrieve`, reuse one
  temporary layout each for other lines, and skip lines at least twice the
  adaptive minimum length. The caller processes skipped/remaining lines after
  the short-line workers join, then applies line heights serially. [N5]
- `LayoutWorker::Start` constructs the text-segment list before dispatch. It
  starts workers only when measured length meets the adaptive minimum, hardware
  concurrency exceeds one, and `CallerMultiThreaded` is absent. Thus block
  workers do not recursively start segment workers. Worker count is
  `min(bytes / 2048, hardware concurrency)`; it is not capped to segment count.
  Workers fetch one segment index atomically at a time, and all workers join
  before the caller accumulates absolute positions and expands tabs. Long-line
  line-break calculation then runs on that caller. [N6][N7]
- Cached valid positions can avoid measurement on a width-only rewrap.
  `LayoutLine` retains positions when text/styles match and only width changes,
  and then recalculates line breaks. The position cache also bypasses native
  measurement entirely for graphic ASCII in a recognized monospace style;
  short-string cache hits return under a cache mutex. Native measurement occurs
  outside that cache mutex, with another lock used to insert results. [N7][N8]
- Original and current Windows use `CreateThreadpoolWork`, submissions, a wait,
  and close for each dispatched group. Linux's portability layer enables
  `std::async(std::launch::async)` and waits on futures instead. These are
  different scheduling mechanisms even though the core's thresholds are
  unchanged. The checked-in core patch wraps `ParallelSupport.h` for portability
  and does not alter these layout algorithms. [N5][N6][N9][P4]

The wxWidgets 3.3.3 implementations explain compatibility-sensitive work in the
adapter: MSW corrects tab advances after `GetTextExtentExPoint`, and GTK walks
Pango cursor clusters then pads partial extents to the Unicode character count.
Replacing those calculations with a cheaper width approximation would change
the contract the current serial oracle preserves. [W1][W2]

## Controlled probes

Temporary Release executables link the existing application/editor libraries.
Both use the current safe native backend. A diagnostic copy of the Core
constructor selects a concurrency cap of 1, 4 or the machine default, 20, and
recomputes the original adaptive budget. No dispatch threshold, cache setting,
font approximation or production source is changed. The cap is not a count of
simultaneously active workers.

Each fixture contains at least 1 MiB of unique styled TOML: either many lines or
one long line, including CJK and non-BMP text. Preparation opens/styles the file
with wrapping disabled and the widget frozen, outside the timed interval. The
first phase enables wrapping, moves to the end and waits for two actual paint
handlers. The second performs 12 alternating native viewport resizes using the
normal caches. Full text and save-point checks pass after every process.
Paint-handler completion is not compositor presentation or proof that every
pending document wrap has finished. `WrapCount` validation itself can invoke
layout and occurs outside the timed intervals. [N16]

All 72 process runs succeeded: per backend, 18 ordinary runs (three repetitions
of each cap/fixture) and six separately instrumented runs (one each). Backends
ran sequentially without overlapping builds. Linux used isolated Xvfb/nested
Weston displays. The table gives ordinary wall-time medians in milliseconds;
instrumentation is absent from these timing results.

| Backend | Fixture and phase | Cap 1 | Cap 4 | Cap 20 |
| --- | --- | ---: | ---: | ---: |
| Windows | Many lines, initial wrap + paints | 874.12 | 892.84 | 902.85 |
| Windows | Many lines, 12 resizes | 4174.26 | 8930.31 | 9907.49 |
| Windows | Long line, initial wrap + paints | 2071.53 | 2266.93 | 2210.84 |
| Windows | Long line, 12 resizes | 567.19 | 530.14 | 577.74 |
| X11 | Many lines, initial wrap + paints | 776.44 | 916.43 | 936.28 |
| X11 | Many lines, 12 resizes | 6636.98 | 11805.93 | 13758.29 |
| X11 | Long line, initial wrap + paints | 638.97 | 1036.91 | 1039.44 |
| X11 | Long line, 12 resizes | 158.56 | 151.23 | 154.35 |
| Wayland | Many lines, initial wrap + paints | 762.59 | 912.03 | 931.25 |
| Wayland | Many lines, 12 resizes | 6601.57 | 11466.39 | 13638.35 |
| Wayland | Long line, initial wrap + paints | 625.03 | 1042.65 | 1037.40 |
| Wayland | Long line, 12 resizes | 377.00 | 378.61 | 377.27 |

The instrumented runs establish the following narrower findings:

- **Native work does not scale in the initial phases.** Within each backend,
  all three caps issue identical measurement-call and byte counts for each
  initial fixture. For example, Windows performs 10,775 calls/783,963 bytes for
  many lines and 29,440 calls/989,718 bytes for the long line. Thousands of those
  calls execute off the GUI thread at caps 4/20, but measured process CPU/wall
  ratios remain around 1.0–1.14, rather than approaching the configured cap.
- **Natural resize timings are not fixed-work throughput.** Windows records
  44,927 / 96,568 / 108,964 measurement calls at caps 1/4/20 during many-line
  resizing. X11 records 89,795 / 142,755 / 162,981; Wayland records
  93,591 / 133,054 / 154,615. The original budget multiplies estimated per-thread
  capacity by `hardwareConcurrency`, and even `wsAll` uses that bounded range.
  Cache use and remaining work can also depend on this scheduling. The observed
  longer interaction sequence therefore includes substantially more native
  measurement, and cannot be attributed entirely to mutex overhead. [N4][N5][N15]
- **Warm long-line resizing mostly bypasses native measurement.** Windows
  records just 25 one-byte measurements and no worker measurement calls across
  12 resizes at every cap; Linux records 24–25 calls and likewise no worker
  calls. Cached positions explain why more layout workers offer little benefit
  in this phase. [N7][N8]
- **The serialized region contains the expensive work.** In the Windows cap-20
  many-line resize profile, held time totals 8,552.92 ms, extent API calls
  8,312.89 ms, and `GdiFlush` 197.34 ms. Flush is required for correctness and
  accounts for only about 2.3% of this held time. On GTK, the counted Pango
  preparation/extents/cursor-attribute region accounts for almost all of the
  measured lock hold; moving byte expansion alone would leave that region
  serialized.

These counters are nested and overlapping, not additive wall-time categories.
Summed worker waits can exceed elapsed time. The MSW native counter wraps only
`GetTextExtent*W`; GTK includes `SetText`, the C++ extents walk and allocations,
and cursor attributes. Their native counters are not cross-platform API-speed
comparisons. GTK hold counts also include constructor locking, while destructor
locking is not timed. Counter overhead can affect the adaptive budget; one
instrumented repetition is diagnostic evidence, not a statistically precise
overhead estimate. The results do not compare the original full application's
GDI and DirectWrite performance.

Raw per-process logs, source transformations, build settings, executable pairs,
the runner and `summary.json` are retained in
`E:/NewNotepad4/wxnotepad-evidence/layout-research-2026-11-13/`.

## Interpretation and feasible follow-up

The initial-phase probes identify shared native measurement as a major serialized
region. The natural resize sequence also exposes work-budget/cache interactions;
its larger measured work means the full slowdown is not an isolated lock-cost
result. Warm long-line cache reuse leaves little native work to accelerate.

The recommended next experiment is worker-owned native measurement state, with
the original core's concurrency and thresholds retained. Reuse resources rather
than create a fresh font map for every span. Measure the same phases and call
volumes, plus memory/cache growth and setup cost; preserve the exact existing
font/DPI/Unicode oracles. Startup/open/save remain separate outcomes. A separate
complete-pending-wrap experiment would be needed to claim full-document
throughput scaling independently of the interactive budget.

Candidates, in priority order, are:

1. Evaluate worker-owned native DC/font or Pango map/context/layout resources.
   This changes ownership and cache lifetime and must measure construction cost,
   cache duplication, memory and DPI/font-configuration behavior as well as speed.
   Per-job allocation can erase the intended gain.
2. Move already-copied, native-state-independent work out of locks where useful,
   preserving exact arithmetic. The probes indicate that byte expansion or the
   MSW tab loop alone will not remove the major serialized measurement cost.
3. Evaluate scheduling reuse or coarser dispatch only if remaining dispatch/join is material,
   preserving partial-layout deadlines, original editing behavior and worker
   execution requirements. Do not disable parallelism to make the test pass.
4. Treat DirectWrite as a separate backend experiment requiring drawing/measurement
   agreement and exact compatibility validation, rather than a drop-in mutex
   removal justified by the original implementation.

No candidate implementation was tested in this investigation. The first is the
best-supported next experiment, not an established speedup. Keep existing
compatibility, concurrency and regression requirements unchanged.

## Primary references

The Notepad4 links are pinned to the inspected revision. Local counterpart:
`E:/NewNotepad4/notepad4/`. Port links are pinned to the inspected HEAD; local
counterpart: `E:/codexworktrees/completion-languages/wxnotepad/`. wx sources were
read locally under `E:/NewNotepad4/wx-v3.3.3/` and linked to the release commit.
Context7 was unavailable in this session; official API docs and local dependency
source were used directly. Probe scope and remaining attribution limits are
documented above.

[N1]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/SurfaceGDI.cxx#L691-L823
[N2]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/SurfaceD2D.cxx#L1460-L1541
[N3]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/src/Notepad4.cpp#L5291-L5293
[N4]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/EditModel.cxx#L217-L221
[N5]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/Editor.cxx#L1571-L1753
[N6]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/EditView.cxx#L365-L534
[N7]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/EditView.cxx#L543-L727
[N8]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/PositionCache.cxx#L1207-L1322
[N9]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/ParallelSupport.h#L11-L12
[N10]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/SurfaceGDI.cxx#L60-L75
[N12]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/SurfaceD2D.cxx#L88-L123
[N13]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/SurfaceD2D.cxx#L318-L323
[N14]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/ParallelSupport.h#L49-L80
[N15]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/Editor.cxx#L1756-L1860
[N16]: https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/Editor.cxx#L6135-L6144
[P1]: https://github.com/1298345095/wxnotepad/blob/05b7d86abd9bb4c290cc342f94ab2c72a6a1b19d/src/editor/platform/wx_platform.cpp#L55-L299
[P2]: https://github.com/1298345095/wxnotepad/blob/05b7d86abd9bb4c290cc342f94ab2c72a6a1b19d/src/editor/platform/text_measurement_msw.cpp#L50-L182
[P3]: https://github.com/1298345095/wxnotepad/blob/05b7d86abd9bb4c290cc342f94ab2c72a6a1b19d/src/editor/platform/text_measurement_gtk.cpp#L28-L247
[P4]: https://github.com/1298345095/wxnotepad/blob/05b7d86abd9bb4c290cc342f94ab2c72a6a1b19d/src/editor/platform/parallel_port.h#L11-L43
[W1]: https://github.com/wxWidgets/wxWidgets/blob/670835aec7e54b9e9d5a50d6a1aea1e8ba0bcdb2/src/msw/textmeasure.cpp#L167-L210
[W2]: https://github.com/wxWidgets/wxWidgets/blob/670835aec7e54b9e9d5a50d6a1aea1e8ba0bcdb2/src/generic/graphicc.cpp#L3070-L3140
[W3]: https://github.com/wxWidgets/wxWidgets/blob/670835aec7e54b9e9d5a50d6a1aea1e8ba0bcdb2/docs/doxygen/overviews/thread.h#L50-L59
[A1]: https://learn.microsoft.com/en-us/windows/win32/procthread/multiple-threads-and-gdi-objects
[A2]: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush#remarks
[A3]: https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nn-dwrite-idwritetextformat#remarks
[A4]: https://learn.microsoft.com/en-us/windows/win32/directwrite/text-formatting-and-layout
[A5]: https://github.com/GNOME/pango/blob/1.52.1/pango/pangocairo-fontmap.c#L149-L187
