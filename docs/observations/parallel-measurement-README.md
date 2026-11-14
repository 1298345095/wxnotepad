# Native parallel text measurement

## Failure and fix

A 4,121-byte styled TOML document with Word Wrap enabled could corrupt font
metrics and crash during repeated native resizing. The pinned core starts block
layout workers at 4,096 bytes and also has a separate adaptive long-line worker
path. The adapter shared a mutable wxDC and wxFont references across workers.

The adapter now captures immutable native font and drawing-context data on the
GUI thread. Workers measure through separate native resources: GDI on Windows,
and a private Pango/Cairo font map, context and layout on GTK. Mutable native
resources have their own locks. Drawing and wx object access remain on the GUI
thread; the original core's hardware concurrency and dispatch thresholds are
unchanged.

A precise concurrent test also exposed a Windows GDI batch handoff problem:
the first byte of a 22-point italic tab sample measured 10 instead of 24, while
its total width remained 48. GDI queues belong to individual threads. Font
restoration now calls `GdiFlush` before releasing the measurement mutex, as
required by Microsoft's
[GDI synchronization documentation](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush).

## Verification

- The original adapter failed the shared-Surface concurrency test. The fixed
  adapter preserves all 324 original serial width/byte-position records exactly
  on Windows, X11 and Wayland, including fonts, styles, Unicode and bitmap scales.
- Four workers perform 64 iterations each against the same Surface. The final
  Windows GDI handoff fix passed 20 consecutive runs without relaxing exact
  equality. The latest test also passes on both Linux backends.
- Native debugger stacks on all three backends show both `WrapBlockWorker` and
  the separate long-line `LayoutWorker` reaching native measurement off the GUI
  thread, followed by a complete regression PASS.
- The long-line fixture contains at least 1 MiB of unique styled items and moves
  the caret to its content end through the public API. Both 80-resize loops,
  original assertions and 120-second timeout remain intact.
- The 10 affected Windows suites and six affected suites per Linux backend pass.
  Canonical clang-format, native clang-tidy and independent Spec/Standards reviews
  are complete. Existing nonblocking analyzer diagnostics remain in retained logs.
- The final actual Windows app, without a debugger, survives 800 rapid native
  resizes of the original user sample. No sample contents are committed.

## Resource observations

Each version/backend ran 22 independent visible Release processes, sequentially
without overlapping builds or other GUI tests: five empty and five small-file
startups, three idle observations per case, and three open/edit/save observations
per 10/100 MiB sample. All saved outputs passed a complete byte comparison.
Dependencies, default fonts and portable INI seeds match within each backend.

The Windows baseline is the retained `6de2684` workaround build, whose production
code is equivalent to `39d0b9a`; the fixed app is `0ceaab4`. Linux reconstructs
the two baseline production objects from `39d0b9a` and compares them with
`32fc9f3`; later changes affect only tests or Windows. Binary hashes, source
manifests and library paths are retained with the raw observations.

Values below are baseline → fixed medians. Windows memory is PrivateUsage;
Linux memory is VmRSS. Compare memory within a backend.

| Backend | Empty startup to Update (ms) | Small startup to native edit (ms) | Idle memory (MiB) | 100 MiB opened memory (MiB) |
| --- | ---: | ---: | ---: | ---: |
| Windows | 196.46 → 195.23 | 261.23 → 258.05 | 9.11 → 10.46 | 118.34 → 118.27 |
| X11 | 71.70 → 71.33 | 102.53 → 102.24 | 94.74 → 94.66 | 200.95 → 200.93 |
| Wayland | 61.45 → 71.65 | 87.20 → 97.35 | 42.66 → 44.20 | 149.71 → 149.92 |

| Backend | 100 MiB open (ms) | Native edit (ms) | Save (ms) |
| --- | ---: | ---: | ---: |
| Windows | 215.56 → 238.85 | 152.40 → 108.14 | 185.62 → 127.59 |
| X11 | 235.82 → 225.42 | 96.78 → 87.00 | 123.36 → 133.52 |
| Wayland | 236.29 → 234.86 | 112.08 → 104.50 | 144.33 → 164.71 |

The existing [resource protocol](../resource-measurement.md) includes observer,
dispatch and 10 ms polling overhead; Update return is not compositor presentation.
Memory snapshots follow one quiet second and are not peaks. OS caches remain
enabled. Linux uses private Xvfb / nested Weston displays and native Linux
storage; Windows uses the owned-topmost option.

This timed process workload uses ASCII plain text with wrapping disabled. A
separate paired Windows replay of the original styled, wrapped TOML sample takes
19.863 → 19.745 seconds for 800 resizes. That single replay includes native
window/paint dispatch and a 3 ms delay per iteration; it is not an isolated layout
throughput benchmark. Startup, save and idle-memory changes above remain visible
in the results. No numerical performance gate was previously agreed.

Full logs, numeric oracles, debugger stacks, binaries and resource reports are
retained locally in `wxnotepad-evidence/parallel-measurement-2026-11-13` on Windows.
