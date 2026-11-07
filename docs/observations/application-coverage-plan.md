# Application coverage before navigation features

Research date: 2026-11-07. Scope: coverage collection before the existing
[editor-interaction batch #149](https://github.com/1298345095/wxnotepad/issues/149) (#150–#156).
Application/build sources were inspected in the preparation worktree; the current CI/Sonar baseline is
[`22f67d4`](https://github.com/1298345095/wxnotepad/commit/22f67d444fdb6ecc3bb890f13e4d32d7460fa3d4).

## Recommendation

Add an independent Ubuntu 24.04 coverage-and-Sonar job with no dependency on the ordinary native-test job.
Build one opt-in coverage tree, reuse the X11/Wayland sharder, generate per-TU GCC 13 `.gcov` reports,
and scan in that same job. Move the existing Linux Sonar step there, retaining one scan location per workflow.
Keep ordinary builds/tests, resource observations, diagnostics and development artifacts on their current
paths. Independent jobs can overlap, subject to runner availability and concurrency limits. [CI] [Jobs]

For these two jobs, the expected critical path is the longer job instead of appending coverage compilation,
native tests and scanning to the ordinary job. That scheduling
benefit is an inference from the job graph; actual turnaround and coverage overhead remain unmeasured.
The cost is another runner, dependency provisioning and application/core/test build. Reuse the dependency
action's Release SDK binary/download caches; cache misses can still duplicate dependency work. [Jobs] [Caches] [Dependencies]
An existing-job coverage tree saves repeated provisioning but serializes the extra build/test pass.
Keeping generation and scanning together avoids cross-job report/SDK transfers in either design.
Reconfiguring the ordinary tree overwrites retained crash binaries; duplicate library targets add link
routing. The independent job preserves ordinary evidence while favoring elapsed CI time. [CI] [Build] [Runners]

## What the current failure established

The CI investigation for [PR #157](https://github.com/1298345095/wxnotepad/pull/157) reported real CFamily
analysis task `0156da1e-addd-42cf-8abb-bf4fa6d9a79a`: zero warnings/open issues and maintainability A.
Its failing coverage was 0/80 for 21 coverable new lines in a C++ test fixture.
Commit `22f67d4` adds `sonar.coverage.exclusions=tests/**`, while keeping C++ fixtures in analysis scope.
That corrects the coverage denominator; application coverage reports still need to be generated. [SonarConfig]

Keep the existing 80% new-code gate and application source scope. Future production changes must be
exercised by real tests. A passing fixture-only analysis supplies no application execution evidence.
Sonar imports reports; it does not collect coverage itself. Coverage also includes condition information,
so an executed line alone does not establish that both outcomes of its condition were exercised. [Import] [Metrics]

## Build and test seam

- Add `WXNOTEPAD_ENABLE_COVERAGE`, default OFF, in `CMakeLists.txt`; enable it only for Linux/GNU.
  Use `.ci/build/coverage-cmake`, `BUILD_TESTING=ON`, diagnostics OFF, and the existing Release SDK/triplet.
  Preserve `tools/build.py` and ordinary `ci.py configure/build/test` defaults. [Build] [Helpers] [Dependencies]
- Apply coverage flags to configured project targets: the staged `notepad4_core`, maintained libraries,
  `wxnotepad`, and C++ test executables. Imported wxWidgets/GTK libraries keep their existing build.
  Instrumenting test translation units also captures maintained inline/header code instantiated there;
  the imported report set and `tests/**` exclusion keep assertion lines out of application coverage.
- Use target-local compile options `--coverage -O0 -g1 -fprofile-abs-path -fprofile-update=atomic`.
  Keep Release configuration and its preprocessor definitions; verify that target `-O0` follows `-O3`.
  This avoids a Debug wxWidgets dependency while improving source-line attribution. [Instrumentation] [Optimization]
- Add `--coverage` to executable link steps, including small console tests such as `date_time_format`.
  Static/object targets need compile instrumentation; their consumers need the gcov runtime at final link.
  CMake compile options do not supply link options. [CMakeCompile] [CMakeLink] [Instrumentation]
- Add focused `coverage-build` and `coverage-report` commands to `.github/scripts/ci.py`, reusing its
  CMake invocation/dependency paths. Reuse `test --cmake-dir` for coverage X11 and Wayland execution.
  Runner isolation permits existing `x11`/`wayland` report names; publish a distinct coverage evidence
  artifact identifying its job/build tree. Keep ordinary and diagnostic report paths unchanged. [Helpers] [Runners]

Build all registered coverage test targets initially and run the existing native suite for both backends.
This reaches application libraries and real-frame/process paths without inventing a second test framework.
Run coverage X11 then Wayland with existing concurrent private-display shards. Ordinary tests overlap on
their separate runner, preserving each job's display and resource ownership. [Helpers] [Build] [Runners]

## Profile lifetime and concurrency

Start the coverage tree clean for each CI run. Collect both backends into the same run's counters, and
generate reports only after every native process has exited. Never reuse `.gcda` across source revisions
or flag changes. GCC writes/merges these files at process exit. [Instrumentation] [Gcov]

Existing shards can execute different binaries that contain the same static-library objects, writing
the same object-associated `.gcda`. GCC supports concurrent processes when the filesystem supports
locking; keep counters on the runner's local filesystem and check for profile-write/mismatch diagnostics.
`-fprofile-update=atomic` addresses threads inside a process; cross-process locking remains a separate
requirement. Keep the existing shard count unless the implementation run exposes a concrete conflict. [Instrumentation]

Use existing orderly-close tests to verify actual-app counter flushing. `observed_shutdown_process.py`
already checks clean exit. `resource_process.py` terminates its child after its protocol assertions;
that path is unsuitable as evidence that app shutdown counters were flushed. Missing counters after
termination remain unexecuted in reports. Coverage collection should use existing clean exits. [Shutdown] [ResourceCheck]

## Report generation and import

Use `g++-13` and the corresponding `gcov-13` from the same runner toolchain; log their versions.
Generate and scan within the same checkout/job so absolute source paths remain valid. Override
`sonar.cfamily.compile-commands` with `.ci/build/coverage-cmake/compile_commands.json`: this job must use
its own real compilation database, generated headers and Release SDK. The ordinary build tree exists
only in the other job. Retain the current source scope and diagnostics setting. [SonarConfig] [CI] [Runners]

Enumerate all coverage-tree `.gcno` files, including objects without `.gcda`; notes-only inputs preserve
never-run translation units. Invoke `gcov-13 -b -c -p -l` separately for each notes file into the report
directory: long names distinguish header contributions from different source files. [Gcov] [ProbeLong]

A GCC 13.3 probe used four concurrent processes/two executables sharing objects and an unlinked notes-only
TU. Counts merged correctly; the untouched return/TU remained `#####`. Shared-header counts used the
valid partial-block suffix (`4*` for four hits), which stays intact in raw reports. The all-notes invocation dropped
header function/branch records, even with `-l`. Per-TU `-l` retained separate header reports with taken
counts 4/0 and never/never. Original counters and all variants are archived with the logs. [Probe] [ProbeLong]

The collector should retain whole, unmodified `.gcov` files whose `Source:` resolves inside repository
`src/`, including maintained/generated headers. It can discard reports for tests, staged upstream core,
system headers and SDK files because those are outside this application-report set. Include zero-hit
records and original branch data. An unrun notes file must still yield uncovered lines; an unreadable or
mismatched notes/data file must fail collection rather than become an empty successful report. [Gcov]

Write the retained files to `.ci/reports/coverage/gcov` and set
`sonar.cfamily.gcov.reportsPath=.ci/reports/coverage/gcov` for the Linux scan. This parameter accepts core
`.gcov` reports, not gcovr XML. Keep `sonar.coverage.exclusions=tests/**`. Preserve collector output,
coverage CTest evidence and the coverage compilation database alongside existing evidence. [Parameters]

Prefer the proven per-TU native output, with Sonar's merge of duplicate-header line/branch records still
requiring acceptance verification. gcovr now has a concrete potential role: aggregate contributions before
generic XML import via `--sonarqube`/`sonar.coverageReportPaths`, if native import loses that information.
It adds a pinned Python dependency and merge/filter defaults to validate. Its current GCC 13 path parses
gcov text, so a JSON workaround cannot be assumed. Compare that bounded alternative if the native import
check fails; gcovr was neither installed nor tested here. [Gcovr] [GcovrMerge] [GcovrParser] [Parameters]

## Implementation order and acceptance

1. Make the CMake/CI helper changes above; extend existing helper invocation tests only for coverage
   build paths, flags and evidence ownership. Keep ordinary full-build/test behavior intact.
2. Add the independent coverage job, reuse the dependency action, and move the Linux Sonar step into it
   after coverage generation. Preserve its current token/event restriction and avoid duplicate analyses.
   Test/report failure must prevent a successful coverage scan; retain failure logs through `always()` evidence upload.
3. In one clean instrumented run, verify a known executed application line has hits and a known untouched
   application branch body has zero hits, then confirm both on the matching Sonar source lines.
   Include an unrun maintained TU; for shared headers, check imported condition counts against known
   source behavior using complementary TU branch outcomes and a repeated-same-outcome control to check
   branch union and deduplication. Line-hit counts alone cannot establish this.
4. For the first real feature among #150–#156, add its behavior tests through the established test seam,
   exercise meaningful successful/empty/boundary outcomes, and inspect imported new-code coverage against
   the unchanged gate. Report imports and assertion execution alone do not establish feature coverage.
5. Record coverage-only provisioning, configure/build, X11, Wayland, report and total workflow durations.
   Use existing build/provenance records to identify the ordinary executable for development artifacts and
   resource observations. `tools/resources.py` already captures build context. [Resources] [Helpers]

Research included the bounded GCC probe above; it ran no application build/native suite or dependency
installation. Application overhead, Sonar's duplicate-header merge and first-feature coverage remain
unmeasured. Linux coverage leaves Windows-only branches to native tests without a Windows coverage report.

[CI]: https://github.com/1298345095/wxnotepad/blob/22f67d444fdb6ecc3bb890f13e4d32d7460fa3d4/.github/workflows/ci.yml
[SonarConfig]: https://github.com/1298345095/wxnotepad/blob/22f67d444fdb6ecc3bb890f13e4d32d7460fa3d4/sonar-project.properties
[Build]: ../../CMakeLists.txt
[Helpers]: ../../.github/scripts/ci.py
[Dependencies]: ../../.github/actions/dependencies/action.yml
[Shutdown]: ../../tests/observed_shutdown_process.py
[ResourceCheck]: ../../tests/resource_process.py
[Resources]: ../../tools/resources.py
[Import]: https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/test-coverage/c-c-objective-c-test-coverage
[Parameters]: https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/test-coverage/test-coverage-parameters
[Metrics]: https://docs.sonarsource.com/sonarqube-cloud/managing-your-projects/metric-definitions
[Instrumentation]: https://gcc.gnu.org/onlinedocs/gcc-13.4.0/gcc/Instrumentation-Options.html
[Optimization]: https://gcc.gnu.org/onlinedocs/gcc-13.4.0/gcc/Gcov-and-Optimization.html
[Gcov]: https://gcc.gnu.org/onlinedocs/gcc-13.4.0/gcc/Invoking-Gcov.html
[CMakeCompile]: https://cmake.org/cmake/help/latest/command/target_compile_options.html
[CMakeLink]: https://cmake.org/cmake/help/latest/command/target_link_options.html
[Gcovr]: https://gcovr.com/en/stable/output/sonarqube.html
[GcovrMerge]: https://gcovr.com/en/stable/manpage.html
[GcovrParser]: https://gcovr.com/en/stable/guide/gcov_parser.html
[Probe]: <E:/NewNotepad4/wxnotepad-evidence/sonar-analysis-config-2026-11-07/gcov-probe/variants.log>
[ProbeLong]: <E:/NewNotepad4/wxnotepad-evidence/sonar-analysis-config-2026-11-07/gcov-probe/per-tu-long.log>
[Jobs]: https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#jobs
[Runners]: https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job
[Caches]: https://docs.github.com/en/actions/concepts/workflows-and-actions/dependency-caching
