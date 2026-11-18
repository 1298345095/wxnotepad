# GitHub Actions

Actions supplement the local build, relevant tests, canonical clang-format and
clang-tidy checks used during implementation. They verify fresh hosted environments
and retain shareable results; they do not replace local verification.

## Workflows

| Workflow | Trigger | Work |
| --- | --- | --- |
| CI | Pull requests, pushes to main, manual | Windows 2025/MSVC and Ubuntu 24.04/GCC 13 builds, all registered CTest tests, development build output, parallel Linux coverage backends and one merged SonarCloud analysis |
| Quality | Same | clang-format 23.1.2, Python syntax, actionlint 1.7.12, native Linux clang-tidy 18 |
| CodeQL | Same | Traced C++ application build and Actions configuration analysis, with results in GitHub Security |

Builds and native clang-tidy use the runner's available CPU count instead of a
fixed two workers. Both entry points accept `--jobs N` for an explicit limit.

The Linux CI job builds once, then partitions each backend's complete selected
CTest list across private sessions, using the available CPU count by default.
Each shard owns its Xvfb, D-Bus and Weston session, focus driver, CTest workspace,
JUnit result and display logs. Tests inside a shard remain serial; shards run
concurrently without sharing native dialogs or clipboard ownership. Xvfb allocates
its own display number with `-displayfd` and reports readiness before the command
starts; each session retains its own authentication cookie. If allocation selects
the reserved primary display `:0`, that owned server stays alive while a second
server allocates the test display. The native-input primary-desktop guard remains
in force. This avoids
`xvfb-run -a` exhausting its ten display-number retries and then launching a client
against another session's display. A dedicated step after dependency installation
connects forty concurrent clients through the actual CI entry point, retaining its
90-second outer guard and the test's original deadlines. Coverage measures the
same regression with the Python helper tests. It runs outside the GUI shards so
its forty sessions do not multiply the configured backend session count. Registration,
commands and original working directories come from the configured CTest tree.
The per-backend test plan records every selected test and its shard. `test --jobs N`
sets the Linux session count. Wayland still runs if X11 tests fail after a successful
build. Standalone resource measurements remain serial. Windows uses one desktop
and clipboard, and runs its full registered suite serially on the hosted VM: the four tests
excluded on the development machine's shared desktop are not silently excluded.

The path-text clipboard fixture seeds distinct owned shaped bytes before each
Copy command. Every command must replace that offer with the original literal
filename/path without shape tags. This avoids reading inherited delayed-render
offers after a publication failure and keeps the original expected-data checks.

Windows native failure captures read desktop pixels only within owned window
bounds before cleanup. Overlapping windows can cover those regions. Native
foreground/active/focus handles recorded before launching the capture helper are
the failure snapshot; the separately labelled window metadata is collected after
helper startup. The helper does not disable or activate the test windows, repaint
them, or pump their events.

Native-dialog fixtures canonicalize temporary-directory roots: Windows dialogs
expand 8.3 aliases such as `RUNNER~1` to their long names. Filename checks use
UTF-8 rather than the runner's local code page. The private display helper claims
an input request before delivering its native event so it cannot erase a newer
request published by an event handler.

Native polling intervals use `tests/steady_stopwatch.h`, backed by
`std::chrono::steady_clock`. UTC corrections must not shorten key-delivery or
dialog waits. During the coverage investigation WSL's UTC clock moved about
13 seconds relative to the monotonic clock in four minutes; wxWidgets' Unix
`wxStopWatch` uses UTC. The existing interval values are preserved. The Linux/GCC
`native_monotonic_wait_clock` fixture wraps clock reads inside its own process
and verifies forward/backward UTC jumps without changing the host clock.
The Modify Lines native dialog uses the same asynchronous compositor-presentation
confirmation as its parent frame before sending input to its multiline fields.

Windows builds an independent untouched pinned Win32 control for its existing
GDI viewport reference and the word-wrap layout reference. The latter replays
all 32 frozen scenes at current DPI and complementary 96 DPI. Frozen original
dialog settings, folds, selection and history remain invariant; native font
metrics and wrapped-row counts come from that same-host original control.
All seventeen wxnotepad fields are compared exactly. The source archive hash,
frozen tables, complete inputs and native output remain in evidence. See
`docs/observations/word-wrap-settings.md` for the approved reference-source change.

Automated IME geometry checks do not establish real Microsoft Pinyin interaction;
the existing human IME records remain applicable and Linux IME remains deferred.
Hosted-runner resource measurements are not performance acceptance thresholds.

## Dependencies and reproducibility

- Actions are pinned to full commit SHAs, annotated with their major versions.
- Linux provisioning has three profiles: `build` for CodeQL, `tidy` for native
  clang-tidy, and `gui` for the build/test matrix. GUI display tools and fonts
  are installed by the `gui` profile, also used by coverage; the analyzer is installed by `tidy`.
- Linux jobs use Ubuntu's official HTTPS archive in place of the Azure archive
  mirror. APT uses finite connection/read timeouts and retries. Downloaded `.deb`
  archives are cached with the existing official cache action, while APT installs
  the packages and runs their configuration scripts on each fresh runner. The
  cache excludes installed system files, package database state and partial downloads.
  Package-download keys include the profile and the existing dependency cache key;
  restore prefixes allow older compatible downloads to be reused. APT resolves
  current package versions normally, including on an exact download-cache hit.
- After installation, the runner owns APT's `partial` download directory. APT
  otherwise leaves it accessible only to `_apt`, which prevents SonarPython's
  package-root discovery from traversing the workspace even outside source scope.
- The vcpkg checkout uses the manifest's builtin baseline and wxWidgets override
  **3.3.3#5**. Only core features are requested, matching the prepared development
  dependencies; unused sound/SDL2 and debug-support features are not requested.
- The wxWidgets overlay disables JPEG, TIFF and WebP codecs and their package
  dependencies on Windows. PNG/SVG art and native bitmap support remain enabled.
  It also disables wxRegEx/PCRE2 and the unused Grid, DataView/TreeList,
  Doc/View and MDI facilities. The application's document model and original
  Notepad4 regular-expression engine do not use those wxWidgets facilities.
  Printing/page setup, native lists/trees and standard dialogs remain enabled.
  Linux retains those facilities. Both platforms disable wxSTC and omit its
  Scintilla/Lexilla source downloads; the application uses the separately pinned
  Notepad4 core and preserves its notices. Local dependency installs need
  the same `--overlay-ports=<repository>/.github/ports` option as CI.
  The wxMSW overlay also locks the pending-watch removal vector shared by the
  UI thread and IOCP worker, preventing concurrent reallocation/erase from
  destroying the same watch twice. `native_file_watcher_lifetime` exercises
  repeated Refresh, pending Stop, restart and destruction during directory writes.
- Linux explicitly requests GTK3's Wayland feature alongside its X11 backend.
  Installing the system GTK development package does not enable Wayland inside
  the separately built vcpkg GTK. Configure records and checks the actual SDK
  backend macros before compiling; the full native suites still exercise both.
- The GTK3 overlay in `.github/ports` preserves the pinned 3.24.52 archive and
  build patches, adding native pathbar button-query cancellation. Explicit vcpkg
  installation selects this overlay; its files participate in the existing cache
  key, and vcpkg's package ABI validates restored binaries. Local dependency builds
  can select the same overlay with `--overlay-ports=<repository>/.github/ports`.
- Host-only GNU gettext 0.22.5 tools are pinned by the manifest baseline. CI
  verifies their versions and extraction/format/release coverage before application
  builds; generated app and locked wxWidgets catalogs accompany every artifact.
  See [localization.md](localization.md) for maintenance and resource layout.
- CI-only triplets build Release dependencies, avoiding unused Debug builds.
  Normal development triplets and the existing local installations are unchanged.
- Windows uses the installed Visual Studio C++ developer shell and Ninja, matching
  vcpkg's toolchain selection rather than hard-coding a Visual Studio generator.
  CMake arguments cross the PowerShell boundary as a JSON array so drive-letter
  paths, spaces and literal characters survive `pwsh -File` parameter parsing.
  Both Windows build entry points request English MSVC output and normalize the
  console input/output code pages to UTF-8 before configuring and building. This
  keeps CMake and Ninja's `/showIncludes` dependency prefix consistent even when
  MSVC has only localized message resources installed.
  A build directory configured with an incorrect localized prefix must be replaced
  with a fresh build directory; setting the language later does not repair its cache.
- Binary packages and downloads are cached by OS, architecture, runner image and
  manifest/triplet/provisioning hashes. vcpkg's own ABI keys validate restored packages. Cached installed
  trees or application build directories are not reused between checkouts.
- The dependency cache is saved after successful installation, before GUI tests.
  A later test failure therefore does not discard an expensive successful build.
- Ordinary Windows/MSVC and Linux/GCC builds opt into sccache 0.19.0 through the
  pinned Mozilla action and CMake's C++ compiler launcher. Diagnostic builds use
  the same cache. The GitHub cache backend stores compilation entries as they
  finish, independently of later test success; there is no application build-tree
  restore or manually maintained precompiled Scintilla library. sccache validates
  compiler, options, source and header inputs. A cold cache or changed inputs still
  requires compilation, and linking/testing always runs normally.
  CodeQL and coverage jobs explicitly disable sccache and clear the CMake launcher:
  CodeQL must observe real compiler invocations, and coverage retains fresh
  instrumentation and counters. clang-tidy configuration also defaults to no
  launcher. The GHA cache namespace is `wxnotepad-cxx-v1`; no additional secret is
  required. The action's own summary and retained `sccache-build.txt` /
  `sccache-diagnostic.txt` show hits/misses; command logs retain monotonic elapsed
  times. CI first checks cold/warm builds, header invalidation and explicit cache
  bypass using a private disk cache, separately from its persistent GHA backend.
  With sccache 0.19.0, Windows caching requires English MSVC diagnostic resources:
  [upstream #2864](https://github.com/mozilla/sccache/issues/2864) corrupts non-UTF-8
  `/showIncludes` output and can silently lose Ninja header dependencies. The
  developer-shell wrapper rejects cache-enabled configuration when the English
  message DLL is missing. Hosted Windows CI has that resource; uncached local
  builds continue to support localized MSVC. The native cache probe documents a
  skip for this explicit rejection and otherwise requires real cache hits and
  header invalidation before CI starts the application build.
  This enables caching only for the hosted CI's ASCII source/dependency paths;
  English diagnostics do not fix upstream corruption of non-ASCII path bytes.
  Local builds remain uncached by default, including checkouts under other paths.
- Core preparation goes through the existing build tool and pinned archive/patch
  checks, with command-local LF settings for archive and patch application so a
  runner's Git autocrlf configuration cannot change prepared bytes. A real-Git regression test runs on
  both hosts before dependency installation and also checks corrupted-archive rejection.
  Windows also checks actual CMake argument forwarding, failure propagation and
  header-triggered Ninja rebuilds through the installed developer shell before
  provisioning dependencies.
  Application CMake configurations explicitly disable manifest installation
  and use the dependency tree provisioned by the workflow.
- CodeQL initializes after dependency installation, then traces compilation of the
  application and original core. It does not trace the entire vcpkg build.

CI and Quality have read-only repository access. Only the CodeQL jobs request
security-events write access. Checkout credentials are not persisted. There are
no deployment credentials, release publication, privileged pull-request triggers,
or self-hosted desktop requirements in these workflows.

## Static-analysis result policy

The formatter is a blocking check. Native clang-tidy analyzes every maintained
translation unit selected from the actual compilation database, covering both
application and test sources while retaining project-header diagnostics.
It runs `clang-analyzer-*`, `bugprone-*`, `performance-*` and `portability-*`.
Compiler errors and core/Unix analyzer findings fail the job. Other check families
remain visible in per-file logs and exported fix records for classification;
their existing API-convention diagnostics are not treated as a warning-free
baseline. CodeQL results are available separately in GitHub Security.

### SonarCloud

The **Linux coverage / x11** and **Linux coverage / wayland** jobs run on separate
Ubuntu runners. Each reuses the dependency action and caches, builds a fresh
`.ci/build/coverage-cmake` with `WXNOTEPAD_ENABLE_COVERAGE`, and runs its complete
native backend suite with the existing isolated display shards. Matrix fail-fast
is disabled so both backends retain their own results and crash evidence.
Ordinary builds and resource observations retain their original binaries.

The **Linux coverage / SonarCloud** job requires both backends to succeed, merges
their coverage, and invokes the pinned official SonarQube Scan action once. It
restores the same SDK with the dependency action's `build` profile, prepares the
pinned original core, and runs `ci.py configure --coverage` to generate the current
commit's compilation database without another application build. The SDK, source
and generated headers referenced by that database exist on the analysis runner.
This analyzes the same Linux compilation variant and coverage options as both
test jobs. Checkout fetches full history for source attribution and PR analysis.

CFamily uses filesystem analysis caching at `.ci/sonar-cfamily-cache`. CI restores
the latest cache for the current PR or branch, falling back to the PR target or
default branch. Every run/attempt gets a new cache key, and the scanner's successful
result is saved before evidence upload. This lets successive PR commits reuse
their own analysis results; the default server cache only supplies the target
branch's results to PR analysis. The first filesystem-cache run populates a new
cache. Source, dependency, compiler and rule changes remain subject to CFamily's
cache validation. Python and other analyzers retain their server-managed caches.

Python analysis targets the workflow's Python 3.12. Python files under `tests/`
are classified as tests; their inclusion pattern also excludes them from the main
source scope. C++ fixtures remain in `sonar.sources` because CFamily does not
analyze files classified through `sonar.tests`. Generated language-scheme tables
retain their existing duplication exclusions and remain analyzed.

The `tests/` tree is excluded from coverage metrics, including C++ fixtures that
CFamily requires in source scope. Those fixtures still receive code analysis.
Application and developer-tool sources remain subject to the configured coverage
gate. Instrumentation is supported on Linux/GCC and is OFF by default. It applies
target-local `--coverage -O0 -g1 -fprofile-abs-path -fprofile-update=atomic` options
to project targets and links the coverage runtime into executables, including
console fixtures. Imported SDK targets keep their normal Release configuration.

The complete X11 and Wayland suites each collect into their own fresh coverage tree. GCC writes
counters on orderly process exit; a forcibly terminated process is not evidence
of executed application lines. Local-filesystem profile locking merges counters
from independent test processes. The existing private-session sharder is reused.
The byte-exact large-file language test has a 120-second guard in coverage builds
and 60 seconds in ordinary builds. The instrumented scenario completed all checks
in 88.50 seconds under controlled CPU contention. Supported Linux CMake versions
use SIGQUIT on timeout so the existing crash collector can preserve a core/stack.

`collect_coverage.py` runs GCC 13's `gcov` separately for every notes file, including
translation units without counters. It preserves unmodified maintained `src/`
reports, zero-hit records and per-translation-unit header branches. GCC's filename
option avoids filesystem name-length limits; source identity comes from each
report's `Source:` record. Corrupt counters fail collection. Reusing an existing
report directory is rejected to prevent stale reports from entering a scan.
The pinned test-only `gcovr==8.8` combines each backend's reports by branch identity
and writes both `sonar.xml` and a full `coverage.json`. The analysis job invokes
`collect_coverage.py --merge` with both JSON reports and writes the final
`sonar.xml` for `sonar.coverageReportPaths`. This preserves complementary outcomes
across translation units and backends without counting repeated outcomes twice.
Throw-branch exclusions are applied when parsing each backend's GCC reports,
before JSON export. Each build retains its own paired GCC notes/counters.
The original `.gcov` files and merger logs remain in the evidence artifacts.
Direct CFamily gcov import was observed to retain only three of four covered
outcomes for `DocumentSession::HasUtf8Bom`, despite all four appearing in the
raw per-translation-unit reports. The merged report includes all four; a second
header control retains its three-of-four result.

Native branch coverage uses gcovr's `--exclude-throw-branches`: GCC-marked
exceptional control-flow edges are excluded from the reported branch denominator.
Ordinary source conditions and exception-handler lines remain measured. All
branches, including throw edges, remain in the preserved raw `.gcov` reports.
This policy was selected after comparing identical counters: native coverage
was 73.18% with all GCC branches and 90.32% with throw edges excluded, with the
same source files and executable lines. The 80% gate and source scope are retained.
See [gcovr's exception-branch explanation](https://gcovr.com/en/8.8/faq.html#why-does-c-code-have-so-many-uncovered-branches).

Coverage tools use fixed test-only dependencies in `.github/requirements-coverage.txt`,
installed in `.ci/coverage-venv`. `.coveragerc` records branch coverage for tools
and CI scripts, including child Python processes. `WXNOTEPAD_COVERAGE_ROOT` gives
children stable source and counter paths even when their working directories
differ. Helper tests and the measured build/test/report entry points contribute
separate data files. The analysis job combines counters from both backends and
its own configuration/report steps before Cobertura XML import through
`sonar.python.coverage.reportPaths`. Unexecuted scripts remain in the report.
The PowerShell source remains analyzed and in coverage scope; this Linux job
does not supply PowerShell coverage. The 80% new-code gate is unchanged.

Configure these project settings before enabling the CI scanner:

1. Add the repository Actions secret `SONAR_TOKEN`, using a SonarCloud token with
   Execute Analysis permission on `1298345095_wxnotepad`.
2. Turn off **Administration > Analysis Method > Automatic Analysis** in that
   SonarCloud project. CI analysis and Automatic Analysis must not run together.

Same-repository PRs, pushes to main and manual runs submit analysis after successful
coverage tests and report generation. A failing test or collector prevents a
successful scan; the existing **Linux coverage / SonarCloud** check explicitly
fails if either backend job fails. Fork PRs retain build/test and coverage checks and skip the
secret-dependent scan step. Scanner failures fail the coverage job. The
SonarCloud quality-gate check remains a separate check.

Each backend uploads a compact `coverage-input-<backend>-<sha>` artifact containing
full native JSON and Python counters. Downloads are scoped to the current workflow
run. Input names are stable across attempts, and rerunning a backend replaces only
its own inputs, so a failed-job rerun can reuse the other backend's successful data.

Read scanner diagnostics in the **Analyze SonarCloud with coverage and compilation
database** step log. Backend evidence artifacts retain raw reports, Python counters,
GCC notes/counters, test and collector logs, and their compilation databases.
The aggregate `evidence-coverage` artifact retains merged reports, original inputs,
the analysis compilation database and `.scannerwork/report-task.txt`, which identifies
the background task.
Investigate encoding warnings from the exact scanner filename and line; keep
UTF-8 configured and avoid excluding source files to suppress an unexplained
warning.

Tests that expect Unicode replacement scalars use C++ Unicode escapes rather
than literal U+FFFD characters. The runtime UTF-8 bytes are identical; Sonar's
source reader otherwise reports these intentional characters as decoding errors.

References: [SonarCloud GitHub Actions setup](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/ci-based-analysis/github-actions-for-sonarcloud),
[CFamily compilation database](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/languages/c-family/running-the-analysis),
[CFamily analysis cache](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/languages/c-family/customizing-the-analysis#analysis-cache).

## Evidence and build output

Windows configures WER full dumps for `visual_brace.exe` and `eol_open.exe`
under `.ci/reports/windows-dumps`. These targets have produced an observed
`0xc0000374` heap failure and a `mixed_line_ending_open` process crash,
respectively. The evidence upload includes that directory, the per-target WER
configuration, the exact test executables and any generated PDBs.
After the complete Windows suite, `ci.py probe` runs the visual brace test up to
20 times with its original 90-second deadline, and the mixed-EOL test up to
three times with its original 30-second deadline. Both use the build working
directory. Each run's combined output and process result remain in
`.ci/reports/visual-brace-repeat` or `.ci/reports/eol-open-repeat`; each sequence
stops and fails on its first abnormal exit. The complete suite's JUnit and
logs remain separate.

The Windows invalid-byte Character Info test runs under an external parent monitor.
If the child is still running after 45 seconds, system DbgHelp captures its thread
stacks, module list and referenced memory under `.ci/reports/windows-dumps`.
The original CTest 60-second guard, child arguments, working directory, output
and numeric exit status remain in force. A kill-on-close job cleans up the owned
child if CTest terminates the monitor. A triggered capture failure is reported
and fails an otherwise successful child result. The evidence upload retains the
matching executable and any PDB. `native_character_info_hang_monitor` verifies
real child exits, GUI output forwarding, dump streams, capture failure and both
hard parent termination and CTest timeout cleanup.

The JSON native fixture waits for GTK's initial folder load before selecting a
file, so that load cannot replace the requested selection. On Linux with CMake
3.27 or newer, its unchanged 60-second timeout sends SIGQUIT with a five-second
dump grace period.
The existing Linux core collector retains its executable and GDB thread stacks.
After the complete Linux suites, a separate private Wayland session repeats this
case up to 20 times, stopping at the first failure; its logs remain separate.

Evidence artifacts retain JUnit XML, complete CTest logs, Weston logs, configure
logs, source provenance and dependency build logs, including on failed jobs.
clang-tidy exports per-file diagnostics and a JSON summary. Linux evidence also
retains the clipboard test executables for native replay. Evidence retention is
30 days; important results should be downloaded before expiry. Development build
output is retained for 14 days, including after a test failure if compilation
succeeded. Each output includes source metadata, the executable SHA-256 and
bundled dependency notices. Artifact names include the run attempt so rerunning
a failed job retains earlier evidence rather than overwriting it.

The artifacts are development builds rather than the planned Windows ZIP release
or Ubuntu installer. Windows DLLs are included beside the executable. Linux
output includes non-system vcpkg shared libraries and a README describing its
Ubuntu 24.04 desktop/runtime requirement. Distribution packaging remains a
separate task.

Linux packaging strips only the distribution copies of the executable and
bundled shared libraries. Their matching separate debug files are retained under
`.ci/symbols/` in the evidence artifact, with GNU debug links in the distributed
binaries. Copy `.ci/symbols/.debug/` beside the executable and `.ci/symbols/lib/.debug/`
inside its `lib/` directory for GDB to find them. The ordinary unstripped
executable is also retained under `.ci/build/cmake/wxnotepad` in the evidence artifact;
the separately compiled diagnostic executable remains preserved as before.
The ordinary test binaries keep their debug information, and build.json records
both the distributed and original executable checksums. These changes do not
alter Linux dependency linkage, test assertions or coverage settings.

CI and packaged-localization checks share
`bundle(cmake_dir, executable, output_root, *, report_dir=None)` in `ci.py`.
The operation returns the packaged executable's path. Its CMake cache selects
the application checkout (`CMAKE_HOME_DIRECTORY`), original-core provenance
(`NOTEPAD4_SOURCE`), dependency installation (`VCPKG_INSTALLED_DIR`) and triplet
(`VCPKG_TARGET_TRIPLET`). The configured installation path is used directly,
including installations whose directory is not named `installed`. DLLs and
catalogs come from the actual executable's parent, supporting Windows `Release/`
and custom runtime output directories. The existing `wxnotepad.exe` and
`wxnotepad` target names select Windows DLL or Linux ELF handling independently
of the triplet's spelling. The output root receives `artifact/` and
Linux `symbols/`; reports use `report_dir` or default to `output_root/reports`.
Packaging reads existing build and SDK inputs directly. Missing inputs and
native-tool failures fail the bundling command and remain visible to its caller.

Newer runs cancel older runs for the same workflow/ref. Open a draft pull request
to validate an integration branch before merge. Branch pushes update the PR run;
push validation runs on main, avoiding duplicate push/PR jobs for the same feature
commit. Manual dispatch remains available. No schedule is installed.

## Local checks

Use the normal documented build entry point and an existing vcpkg installation.
For the additional checks:

```text
python -X utf8 .github/scripts/checks.py format
python -m compileall -q .github/scripts tools tests
python -m unittest discover -s tests -p 'test_*.py' -v
actionlint
python3 .github/scripts/checks.py tidy --build-dir /path/to/native/cmake-build
```

The formatter command requires clang-format 23.1.2 on PATH. The analyzer defaults
to clang-tidy-18; use `--executable` for another installed native analyzer. Keep
source and compilation-database paths consistent. A local MSVC analysis needs its
normal Visual Studio developer environment.

To collect development build output from an existing configured native build,
provide its CMake directory and actual executable, then choose output and report
locations:

```text
python -X utf8 .github/scripts/ci.py bundle --cmake-dir "F:/native build/cmake" --executable "F:/native build/cmake/Release/wxnotepad.exe" --output-dir F:/native-package --report-dir F:/evidence/bundle-attempt-1
```

On Linux, use the native build's `wxnotepad` executable and Linux paths with the
same flags. `--output-dir` owns `artifact/` and `symbols/`; `--report-dir` selects
the packaging evidence directory, including Linux runtime-library listings.
Omitting `--report-dir` uses `<output-dir>/reports`. The command creates the
required output directories.
With no options, `ci.py bundle` uses `.ci/build/cmake`, its native application
executable, and the existing `.ci/artifact`, `.ci/symbols` and `.ci/reports` layout.
With only `--cmake-dir`, the executable defaults to that directory's native
`wxnotepad` target; pass `--executable` for configuration or custom output paths.

To exercise the exact CI test wrapper against an existing native build:

```text
python3 .github/scripts/ci.py test --backend x11 --cmake-dir /path/to/native/cmake-build
python3 .github/scripts/ci.py test --backend wayland --cmake-dir /path/to/native/cmake-build
```

On Windows, use the same wrapper with `--backend windows`. Add a fresh
`--report-dir` to preserve each local attempt, and `--tests` to select registered
cases. Windows uses the Default input desktop and serial execution; the private
X11/Wayland sessions retain their existing concurrency.

The original registered path-text case can be selected against the configured CI build:

```text
python -X utf8 .github/scripts/ci.py test --backend windows --tests '^original_path_text_clipboard$' --report-dir .ci/reports/local-path-text-01
```

For a standalone owned executable outside CTest, the synchronous probe entry
point records each numeric process exit code, deadline and raw output:

```text
python -X utf8 .github/scripts/ci.py probe --report-dir F:/evidence/probe-attempt-1 --timeout 30 --runs 2 -- "F:/native build/sdk_capabilities.exe"
```

Use `--cwd` when the executable requires another working directory. Arguments
after `--` are passed literally without an extra shell. Runs are serial; a
nonzero exit, launch failure or timeout stops the sequence and fails the command.
The summary is successful only when every requested process completed with exit
zero. A fresh report directory is mandatory; earlier failures are never replaced
by later runs. For tests with peer processes, keep using the registered CTest
entry point and its existing lifecycle/timeout handling.

The `native_probe_process_runner` regression exercises real child exits,
timeouts, launch failures and repeated runs, including the actual GUI SDK probe.
Its native logs remain under `Testing/Temporary/native-probe` and are collected
with the existing evidence artifacts.

SDK validation has two complementary parts. The registered
`sdk_retained_capabilities` case consumes the actual wx headers/libraries,
checks the trimmed build profile, creates native controls and printing/page
setup objects without printing, round-trips zlib/PNG, and rasterizes SVG art.
The CI bundle check inventories the selected SDK and actual runtime output:

```text
python -X utf8 .github/scripts/check_sdk.py --cmake-dir F:/native-build/cmake --artifact-dir F:/native-package/artifact --output F:/evidence/sdk-attempt-1.json
```

The installation and triplet come from that build's CMake cache, and must agree
with its selected wxWidgets configuration. Missing headers or core/base libraries
fail rather than producing an empty successful inventory. wxSTC/Scintilla/Lexilla
SDK artifacts fail on both platforms. Windows runtime output also rejects the
removed JPEG/TIFF/WebP/PCRE2 libraries; the Linux GTK dependency profile is
unchanged. Omitting `--artifact-dir` explicitly checks only the SDK and records
`runtime_checked: false`.

CI enables `WXNOTEPAD_VERIFY_TRIMMED_SDK=ON` for the compile-time trimming
assertions. Enable the same option when validating a local overlay-built SDK.
It defaults to OFF for ordinary development installations, whose unused wx
features may remain enabled; retained-capability runtime checks still run.

For Codex on this Windows host, `shell: sh` selects Git Bash and `shell: bash`
selects WSL Ubuntu. In WSL commands use Linux paths such as `/mnt/f/...` while
retaining a Windows `workdir` for the host tool. The Python runner can read its
`--help` in either environment; choose the interpreter, native build and SDK of
that same environment. Pass environment changes as quoted values or a structured
process environment; an inherited PATH can contain spaces and parentheses.
The Wayland driver needs the intended Weston installation on PATH.

`--tests` optionally selects a CTest regex for local diagnosis. The workflows omit
it and run the complete registered suite. CI helper provisioning commands use
the task-local `.ci/` tree; normal development can continue using the existing
build tool and installed dependencies.
