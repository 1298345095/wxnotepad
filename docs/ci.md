# GitHub Actions

Actions supplement the local build, relevant tests, canonical clang-format and
clang-tidy checks used during implementation. They verify fresh hosted environments
and retain shareable results; they do not replace local verification.

## Workflows

| Workflow | Trigger | Work |
| --- | --- | --- |
| CI | Pull requests, pushes to main, manual | Windows 2025/MSVC and Ubuntu 24.04/GCC 13 builds, all registered CTest tests, development build output, independent Linux coverage and SonarCloud job |
| Quality | Same | clang-format 23.1.2, Python syntax, actionlint 1.7.12, native Linux clang-tidy 18 |
| CodeQL | Same | Traced C++ application build and Actions configuration analysis, with results in GitHub Security |

Builds and native clang-tidy use the runner's available CPU count instead of a
fixed two workers. Both entry points accept `--jobs N` for an explicit limit.

The Linux CI job builds once, then partitions each backend's complete selected
CTest list across private sessions, using the available CPU count by default.
Each shard owns its Xvfb, D-Bus and Weston session, focus driver, CTest workspace,
JUnit result and display logs. Tests inside a shard remain serial; shards run
concurrently without sharing native dialogs or clipboard ownership. Registration,
commands and original working directories come from the configured CTest tree.
The per-backend test plan records every selected test and its shard. `test --jobs N`
sets the Linux session count. Wayland still runs if X11 tests fail after a successful
build. Standalone resource measurements remain serial. Windows uses one desktop
and clipboard, and runs its full registered suite serially on the hosted VM: the four tests
excluded on the development machine's shared desktop are not silently excluded.

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
  **3.3.3#1**. Only core features are requested, matching the prepared development
  dependencies; unused sound/SDL2 and debug-support features are not requested.
- Linux explicitly requests GTK3's Wayland feature alongside its X11 backend.
  Installing the system GTK development package does not enable Wayland inside
  the separately built vcpkg GTK. Configure records and checks the actual SDK
  backend macros before compiling; the full native suites still exercise both.
- The GTK3 overlay in `.github/ports` preserves the pinned 3.24.52 archive and
  build patches, adding native pathbar button-query cancellation. Explicit vcpkg
  installation selects this overlay; its files participate in the existing cache
  key, and vcpkg's package ABI validates restored binaries. Local dependency builds
  can select the same overlay with `--overlay-ports=<repository>/.github/ports`.
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

The independent **Linux coverage / SonarCloud** job invokes the pinned official
SonarQube Scan action after its native tests and report collection. It reuses the
dependency action and caches, and builds `.ci/build/coverage-cmake` with the
opt-in `WXNOTEPAD_ENABLE_COVERAGE` CMake option. Ordinary builds and resource
observations retain their original binaries. `sonar-project.properties` selects
the coverage tree's real compilation database; scanning happens on that same
runner while its SDK, prepared original core and generated headers still exist.
This analyzes the Linux compilation variant. Checkout fetches full history for
source attribution and PR analysis.

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

The complete X11 and Wayland suites collect into a fresh coverage tree. GCC writes
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
The pinned test-only `gcovr==8.8` then combines those reports by branch identity
and writes `sonar.xml` for `sonar.coverageReportPaths`. This preserves complementary
outcomes across translation units without counting repeated outcomes twice.
The original `.gcov` files and the merger log remain in the evidence artifact.
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
separate data files, combined before Cobertura XML import through
`sonar.python.coverage.reportPaths`. Unexecuted scripts remain in the report.
The PowerShell source remains analyzed and in coverage scope; this Linux job
does not supply PowerShell coverage. The 80% new-code gate is unchanged.

Configure these project settings before enabling the CI scanner:

1. Add the repository Actions secret `SONAR_TOKEN`, using a SonarCloud token with
   Execute Analysis permission on `1298345095_wxnotepad2`.
2. Turn off **Administration > Analysis Method > Automatic Analysis** in that
   SonarCloud project. CI analysis and Automatic Analysis must not run together.

Same-repository PRs, pushes to main and manual runs submit analysis after successful
coverage tests and report generation. A failing test or collector prevents a
successful scan. Fork PRs retain build/test and coverage checks and skip the
secret-dependent scan step. Scanner failures fail the coverage job. The
SonarCloud quality-gate check remains a separate check.

Read scanner diagnostics in the **Analyze SonarCloud with coverage and compilation
database** step log. The separate coverage evidence artifact retains raw reports,
Python counters, GCC notes/counters, test and collector logs, compilation database
and `.scannerwork/report-task.txt`, which identifies the background task.
Investigate encoding warnings from the exact scanner filename and line; keep
UTF-8 configured and avoid excluding source files to suppress an unexplained
warning.

Tests that expect Unicode replacement scalars use C++ Unicode escapes rather
than literal U+FFFD characters. The runtime UTF-8 bytes are identical; Sonar's
source reader otherwise reports these intentional characters as decoding errors.

References: [SonarCloud GitHub Actions setup](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/ci-based-analysis/github-actions-for-sonarcloud),
[CFamily compilation database](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/languages/c-family/running-the-analysis).

## Evidence and build output

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

To exercise the exact CI test wrapper against an existing native build:

```text
python3 .github/scripts/ci.py test --backend x11 --cmake-dir /path/to/native/cmake-build
python3 .github/scripts/ci.py test --backend wayland --cmake-dir /path/to/native/cmake-build
```

`--tests` optionally selects a CTest regex for local diagnosis. The workflows omit
it and run the complete registered suite. CI helper provisioning commands use
the task-local `.ci/` tree; normal development can continue using the existing
build tool and installed dependencies.
