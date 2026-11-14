# Worker measurement prototype results

Recorded 2026-11-13/14. Candidate backend commit: `7f5035d`.
Baseline: `81515bf` (measurement code equivalent to `05b7d86`).

## Decision

Independent native measurement resources preserve the observed numeric results
and remove the shared-resource bottleneck on Linux. Windows gains are smaller.
The experiment supports pursuing the design, but does not authorize production
adoption: retained font caches add measurable idle memory, and the X11 long-line
resize comparison is unresolved. No memory budget has been agreed.

The candidate retains GDI flushing and original Unicode/position mapping. Each
Windows thread owns reusable native resources; fonts are keyed by thread and
height. GTK workers exclusively lease independent Pango resources, then return
them to a GUI-owned pool when short-lived async threads exit. The pool preserves
native font caches, which explains both the speed and the retained-memory cost.

## Method and limits

Same compiler settings, fonts, baseline libraries and synthetic 1 MiB TOML
fixtures; three independent process repetitions with interleaved variant order.
The original batch has 72 processes; the controlled X11 retake adds 12. Caps 1
and default 20 are diagnostic constructor hooks in temporary benchmark code only.
The application keeps its original policy.

Initial phase: enable wrapping, go to the end and complete two native paint
handlers. Resize phase: 12 alternating natural window resizes with normal caches.
These are handler/interaction observations, not compositor presentation latency
or proof that every pending line has wrapped. Adaptive core budgets can change
the amount of work performed in the resize sequence; these are not fixed-work
throughput measurements. Resource checkpoints occur outside timed intervals
after one quiet second. Full text and save-point assertions passed in every run.

## Default-cap medians

Elapsed milliseconds, baseline → candidate:

| Backend | Multiline initial | Multiline 12 resizes | Long-line initial | Long-line 12 resizes |
| --- | ---: | ---: | ---: | ---: |
| Windows | 966.53 → 703.92 | 9352.98 → 7807.55 | 2238.39 → 1943.87 | 599.24 → 572.82 |
| Wayland | 905.28 → 107.07 | 13605.90 → 2110.40 | 1063.30 → 110.57 | 365.15 → 368.34 |
| X11, controlled viewport | 960.55 → 99.79 | 16380.10 → 2970.82 | 1094.27 → 112.08 | **Rejected: native allocation differs** |

Linux initial layout is roughly 8–10× faster, with multiline resize sequences
roughly 5.5–6.4× faster. Windows multiline initial and resize sequences improve
by roughly 1.37× and 1.20×. Wayland long-line resize time is effectively unchanged.
The single-worker diagnostic runs show no comparable benefit.

CPU time divided by wall time indicates roughly 15–20 utilized cores for Linux
multiline candidate phases versus about 1.1 for baseline; Windows uses about
2.6–3.1 versus 1.1. CPU cost increases, and adaptive work budgets differ. The
configured cap of 20 must not be reported as 20 simultaneously active workers.

## Resource cost

After multiline work followed by New Document, median memory:

| Backend/counter | Baseline MiB | Candidate MiB | Increase MiB |
| --- | ---: | ---: | ---: |
| Windows private usage | 14.57 | 18.88 | 4.31 |
| Wayland RSS | 51.76 | 71.65 | 19.89 |
| X11 controlled retake RSS | 101.46 | 119.96 | 18.50 |

Initial blank-window memory is essentially unchanged. Windows GDI object counts
after resizes rise from 118 to 168; after New Document, 117 to 147. Linux RSS and
Windows private usage are different counters and cannot be compared directly.
Cache lifetime/retention needs deliberate treatment before production adoption.

## X11 validation failure retained

The original long-line runs produced different wrap counts, 12586 and 12328, in
both variants. The original summarizer still fails and writes
`validation-failures.json`; those comparisons are not accepted.

Functional probes traced a difference between the editor's outer size, wxGTK's
reported client size and the GTK child drawing allocation. With overlay scrolling
enabled, an 800-wide outer window could report client widths 792 or 798 and
corresponding core wrap widths 752 or 758. Disabling overlay scrolling in the
experiment window stabilized eight functional observations at 12587 wraps.

The authorized retake records 14 geometry observations per process and enforces
`wrapWidth == clientWidth - textStart - rightMarginWidth`. All final CHECK values
agree. All multiline geometry observations agree across six runs, as do the two
initial long-line observations. During long-line resizing, however, the native
drawing allocation still differs (585 versus 785) even where reported client
and core widths agree. The long-line resize timings remain rejected. Two paint
handlers plus outer/client-size checks do not prove native allocation readiness.
This is a harness limitation; no matching upstream wxWidgets defect was confirmed.

## Correctness and review

- All 324 serial width/byte-position records exactly match the prior safe backend
  on Windows, X11 and Wayland.
- Windows concurrent measurement passes 20 consecutive runs of the existing
  four-worker × 64-iteration check.
- Existing measurement, wrapped-resize and font checks pass on all three backends,
  retaining both 80-resize loops and the existing timeouts/assertions.
- The GTK native oracle passes 162 font/encoding/text combinations per run with
  four workers, normal/HiDPI X11 and Wayland, memory scale and nonuniform transform.
- Native clang-tidy and canonical clang-format checks completed. Independent
  Spec and Standards reviews each reported zero actionable findings.

Reproduction sources and the verdict are captured on the throwaway branch.
Raw evidence, including rejected groups, remains in the Windows evidence folder
listed in [README.md](README.md). No production merge decision is made here.
