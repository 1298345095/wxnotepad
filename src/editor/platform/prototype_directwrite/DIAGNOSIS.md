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
listing now shows the marker GUID after its braces were removed. The next
capture keeps the probe at the original standard-user integrity level.
