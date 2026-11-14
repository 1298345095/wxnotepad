# PROTOTYPE: independent native worker measurement

Throwaway experiment on `codex/prototype-worker-measurement`. Question: can workers
reuse independent GDI/Pango resources while preserving exact measurements and the
original core's parallel layout policy? See [RESULTS.md](RESULTS.md) for the answer,
resource costs and the rejected X11 comparison. This is not production adoption.

The two adjacent `text_measurement_*.cpp` files contain the candidate. This folder
captures the native benchmark and its build entry point. The normal application
never reads the diagnostic worker cap. No scheduler, threshold or correctness
assertion was relaxed.

## Build

Use the project's existing wxWidgets 3.3.3 toolchain and prepared pinned core.
`PORT_BUILD` **must contain baseline libraries built from `81515bf` or its
code-equivalent `05b7d86`**, before the worker prototype. Linking a candidate build
there invalidates the comparison. Use the same compiler, Release configuration
and dependencies for both variants. The harness inserts the candidate object
ahead of the baseline archives, leaving all other code shared.

From the prototype repository root in Git Bash on Windows:

```sh
pwsh -NoProfile -File .github/scripts/msvc.ps1 -CMakeArgumentsJson '["-S","src/editor/platform/prototype_worker_measurement","-B",".ci/worker-prototype-capture","-G","Ninja","-DCMAKE_BUILD_TYPE=Release","-DCMAKE_TOOLCHAIN_FILE=G:/vcpkg/scripts/buildsystems/vcpkg.cmake","-DVCPKG_MANIFEST_MODE=OFF","-DPORT_BUILD=E:/codexworktrees/completion-languages/wxnotepad/.ci/native-windows","-DCORE_ROOT=E:/codexworktrees/completion-languages/wxnotepad/.ci/build/original-core"]'
pwsh -NoProfile -File .github/scripts/msvc.ps1 -CMakeArgumentsJson '["--build",".ci/worker-prototype-capture","--parallel","6"]'
```

Linux uses the same CMake source with `-DCMAKE_BUILD_TYPE=Release`, the native
vcpkg toolchain, and native Linux `PORT_BUILD`/`CORE_ROOT` paths. Keep the source
and build on the Linux filesystem. Do not translate Git Bash `/e/...` paths into
WSL paths; Windows drives appear under `/mnt/e/...` in WSL.

`prepare_sources.py` creates temporary copies in the build directory: one core
constructor hook for diagnostic caps, and one benchmark with geometry logging
and GTK overlay scrolling disabled **only in its test window**. Real core source
and application settings remain untouched.

Targets `worker_metrics`, `worker_resize` and `worker_fonts` reuse existing
correctness checks and their original 90/120/20-second timeouts. `worker_app`
provides a manual candidate application. Building does not start a benchmark.

## Run only after a fresh performance-ready response

The recorded performance window has ended. Notify the user and wait until they
have closed high-load applications before any new timing/resource run.

After that response, one command launches the original interleaved Windows batch
(use a new output directory):

```sh
python src/editor/platform/prototype_worker_measurement/run-comparison.py .ci/worker-prototype-capture .ci/worker-comparison --suffix .exe
```

For Linux, run the launcher inside the existing private display helper, separately
for `x11` and `wayland`, with the project's Weston helper on PATH:

```sh
xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' dbus-run-session -- python3 tools/clipboard_isolated.py x11 python3 src/editor/platform/prototype_worker_measurement/run-comparison.py .ci/worker-prototype-capture .ci/worker-comparison-x11
```

The controlled X11 variant adds
`--caps 0 --baseline-name baseline_fixed_layout --candidate-name worker_fixed_layout --require-geometry`.
Its native allocation readiness remains incomplete for long-line resizes; do not
use that group as accepted performance evidence without fixing the harness.

The launcher preserves logs, executable hashes, timings, memory and CHECK values
after every process. Its `ok` field checks process completion and record shape,
**not cross-run geometry equivalence**. Compare CHECK values and all geometry
records across versions and repeats before accepting a result. A matching wrap
count alone does not establish a matching native viewport.

Measurement-detail counters inherited from the earlier research header are zero
in these uninstrumented runs. Use `wall_ns` and `cpu_ns`; `threads` is the configured
core cap, not the number of active workers. Preserve failures as well as passes.

## Evidence

Raw logs, binary artifacts, native measurement oracles, reviews and original
failed reports are retained outside Git at
`E:/NewNotepad4/wxnotepad-evidence/worker-measurement-prototype-2026-11-13/`.
The build capture was subsequently compiled without repeating performance runs.
