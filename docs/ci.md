# GitHub Actions

Actions supplement the local build, relevant tests, canonical clang-format and
clang-tidy checks used during implementation. They verify fresh hosted environments
and retain shareable results; they do not replace local verification.

## Workflows

| Workflow | Trigger | Work |
| --- | --- | --- |
| CI | Pull requests, pushes to main, manual | Windows 2025/MSVC and Ubuntu 26.04/GCC 15 builds, all registered CTest tests, development build output, parallel Linux coverage backends and one merged SonarCloud analysis |
| Quality | Same | clang-format 23.1.2, Python syntax, actionlint 1.7.12, native Linux clang-tidy 21 |
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
server allocates the test display. Xvfb uses `-noreset` to keep new connections
available when sequential GUI processes close the display's last client. The
native-input primary-desktop guard remains
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
Foreground metadata includes the native window class, process ID and process
image so a competing window can be identified without capturing its pixels.
The Windows `native_windows_failure_capture` regression compares the configured
Python path with its compiled value and launches the real capture helper against
an owned hidden window. CMake normalizes path separators before embedding the
Python executable in a C++ string literal.
Ordinary Windows CI runs the complete registered suite once. The historical
context-menu prefix replay, hundred first-pointer processes and twenty complete
menu processes are available only through the manual `ci-swap-prefix`
investigation below. The complete suite retains all menu and clipboard assertions,
native input paths and original timeouts. Missing clipboard-peer responses retain
their protocol stage and failure capture, with the failed read frozen before
collection. Failure screenshots, native metadata and test reports remain in the
ordinary evidence artifact.
The clipboard peer also accepts shutdown while an empty native paste is pending;
the failed paste is not answered with a successful result. The registered
`native_windows_clipboard_peer_lifecycle` case checks this path with the real peer
and verifies that it exits after acknowledging shutdown.

On Windows the stock clipboard peer reserves clipboard access immediately before
its single native EDIT paste, after wx's capacity preflight, using the remainder
of its existing three-second activation budget. The control uses its platform
maximum text length so a busy optional length probe cannot truncate fixture data.
The lifecycle case also holds the clipboard in a foreign process for 250 ms and
checks an exact Unicode payload above 64 KiB. The original empty-paste shutdown
checks and 30-second case guard remain. IPC readers wait for readable UTF-8
responses within their original five-second deadline, including transient Windows
sharing violations; the document and clipboard result assertions are unchanged.

The Windows editor fixture grants foreground permission to its specific clipboard
peer PID before publishing a request; the foreground peer grants it back to the
editor PID before publishing its response. `AllowSetForegroundWindow` only grants
permission and does not activate either window. Each sender must actually own the
foreground, and the existing single activation call and native foreground/focus
assertions still decide whether the handoff succeeded. Failed editor activations
retain both grant receipts and the effective foreground-lock timeout.

### Focused Windows investigations

During diagnosis, use `windows_context_investigation.py` against a configured
Windows build. It selects registered CTest cases and uses the existing Windows
desktop guard, serial execution, assertions and case timeouts. Every attempt
keeps a separate JUnit/log directory; a failed attempt stops further attempts.
Report roots must be fresh. The saved `plan.json` identifies the actual test
selection and the native targets built for it.

| Scenario | Registered cases | Build scope |
| --- | --- | --- |
| `prefix` | Original order through `original_editor_context_menu` (currently 43 cases) | Fourteen inspected predecessor targets and their existing dependencies |
| `clipboard` | Complete `original_editor_context_menu`, repeated without interleaving other cases | `context_menu`, including its `clipboard_peer` dependency |
| `swap` | Existing `original_clipboard_swap`, retaining its 180-second case timeout | `clipboard_smoke`, including its `clipboard_peer` dependency |
| `swap-prefix` | Original registered order through `original_clipboard_swap` (currently 87 cases) | Thirty-nine inspected predecessor targets and their existing dependencies |
| `ci-swap-prefix` | Historical context-menu prefix, 100 pointer processes and 20 complete menus, then the registered order through Swap | Same targets as `swap-prefix` |
| `focus-transfer` | One hundred native Ctrl+K exchanges, validating Unicode bytes, reversed selection and both real foreground handoffs | `clipboard_smoke` and its existing peer dependency |
| `search` | Original 13-scenario search-continuation case, retaining its 60-second timeout | `search_continuation`; hosted investigation keeps optimized debug symbols and WER crash dumps |

On Windows, `focus-transfer` uses the existing keyboard-state and native
accelerator translation path; it does not insert physical keys with `SendInput`.
Both clipboard endpoints additionally require their top-level HWND to equal
`GetForegroundWindow()`. The original first-pointer probe still uses `SendInput`.

The prefix target list is explicit because CTest omits command paths for unbuilt
executables. Update `PREFIX_TARGETS` when a new predecessor requires another
program. Test selection still comes from the current CTest registration.

CLI report and foreground-state destinations must resolve within the invocation's
working directory, including through symlinks. Build input paths can remain
external. To store reports on F: from Git Bash, invoke the absolute script from
the selected evidence directory:

```bash
repo_path=$(pwd -W)
mkdir -p /f/evidence
cd /f/evidence
python -X utf8 "$repo_path/.github/scripts/windows_context_investigation.py" build --scenario prefix --cmake-dir F:/native-build/cmake --report-dir context-build-01
python -X utf8 "$repo_path/.github/scripts/windows_context_investigation.py" run --scenario prefix --runs 3 --cmake-dir F:/native-build/cmake --report-dir context-prefix-01
```

Use the `plan` action to inspect the selection without compiling or opening test
windows. Use `--scenario clipboard` for iterations confined to the clipboard peer.
The peer-shutdown regression remains in the ordinary full suite and can be
selected separately through `ci.py test` when that behavior changes; it does not
alter clipboard state between the reproduction's complete-menu processes.
An applicable earlier full-suite result remains evidence for unchanged code;
run related cases during diagnosis, then the complete required checks on the
final proposed fix.

The separate **Native investigation** workflow is manually dispatched. Its
Windows scenarios use the same Windows 2025 runner, pinned SDK, compiler cache
and display conditions. Its default is three prefix
attempts, with the original independent reference controls run first. It builds
only the selected native targets, without coverage, packaging or a complete
platform suite. Keep investigation branches without a pull request
so their pushes do not trigger the ordinary PR workflows. Pushes alone do not
start the investigation either. Transfer final changes to the repair PR once
ready for complete validation.

Dispatch the registered workflow against an investigation branch, for example:

```text
gh workflow run windows-investigation.yml --ref codex/windows-context-investigation-2026-11-24 -f scenario=clipboard -f runs=20
```

To replay the former CI context-menu prelude before Swap, select
`scenario=ci-swap-prefix` and `runs=1`. Each attempt preserves separate prefix,
pointer, complete-menu and Swap-prefix reports and stops at the first failure.
The pointer probe retains its original native gesture and reversed-selection
checks; the complete-menu repetitions retain the registered CTest case and its
sixty-second timeout. Use `zero_foreground_timeout=true` to match the foreground
policy prepared by ordinary hosted Windows CI.

Its **Windows context investigation** check is separate from required merge
checks. The ordinary CI/Quality/CodeQL/Fuzz workflows and full acceptance remain
unchanged; a focused success does not establish full-suite acceptance.

The optional `hide_terminal` workflow input defaults to false. Set it to true
for an isolated hosted comparison after terminal foreground interference has
been observed. The runner hides only verified WindowsTerminal.exe windows with
the observed Cascadia class; it keeps their processes running and records the
before/after visibility and process state in `terminal-isolation.json`. The same
registered cases, gestures, response deadlines and assertions still run.

The separate `zero_foreground_timeout` input is an opt-in hosted experiment.
It reads the Windows foreground lock timeout, attempts to set it to zero for
that desktop session, verifies the effective value, and restores the original
setting in the runner's `finally` block. `foreground-timeout.json` retains the
setter, readback and restoration results. It does not persist the setting in
the user profile, and the CLI rejects this option outside GitHub Actions.
An already-zero value or rejected setter cannot establish that foreground lock
caused a test failure. This does not change test deadlines or assertions. See
[the primary-source research](observations/windows-ci-foreground.md).

Ordinary hosted Windows CI uses the same foreground-session helper before its
native checks, then restores the original value in an `always()` cleanup step.
It records both values in `windows-foreground.json` and does not change local
developer desktop settings. Native focus helpers keep the application's input
queue independent; they do not attach it to Terminal or clipboard peers.
The runner's measured original timeout was 2,147,483,647 ms, so ordinary
foreground activation was rejected after the first process. With a temporary
zero timeout and no input attachment, 20 complete menu cases passed with
Terminal hidden, then 20 passed with Terminal visible. The second run also
recorded existing successful cross-process handoffs. Test inputs, assertions,
menu/peer deadlines and CTest timeouts remain unchanged.

Native-dialog fixtures canonicalize temporary-directory roots: Windows dialogs
expand 8.3 aliases such as `RUNNER~1` to their long names. Filename checks use
UTF-8 rather than the runner's local code page. The private display helper claims
an input request before delivering its native event so it cannot erase a newer
request published by an event handler.

Packaged X11 window-routing activation waits for the owned process's unique
visible native window before minimizing it. wxWidgets' `shown` publication can
precede Openbox's map/reparent sequence. This setup check has a 15-second
monotonic deadline; process exit, ambiguous windows and native query errors still
fail. The original minimize, delivery and restoration checks and 120-second case
guards remain. `native_x11_routing_map_readiness` runs the complete activation
case while its own private Openbox holds the initial map through startup
publication, then resumes it after 100 ms. This X11-only regression skips on
Wayland. Managed routing evidence now lives under `Testing/Temporary/`, which the
existing artifact upload retains. See [the diagnosis](observations/x11-routing-map-readiness.md).

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

Independent Windows Explorer investigation controls remain available manually:

```text
python -X utf8 .github/scripts/ci.py probe --report-dir .ci/reports/open-target-controls/process --timeout 180 -- python -X utf8 tests/reference_capture/native_original_reference.py --archive .ci/build/notepad4-source.tar --build-dir .ci/original-reference --open-targets-output-dir .ci/reports/open-target-controls --compiler-cache
```

Each uses a fresh owned directory and the read-only exact-item observer with the
application test's observation deadlines. Reports retain every verdict and the
pinned original helper's source hashes, even when an earlier control fails.
Actual Windows Server evidence showed the pinned helper and documented
single-item cold-view calls losing selection, while the same one-call selection
succeeded after native folder readiness. The additional cold Explorer startup
control exercises the original disabled fallback's quoted target grammar with
Unicode, a comma and a space. The temporary automatic investigation steps have
been removed; the ordinary product suite, original references and failure
captures remain required. These historical cold-control failures are retained
as evidence and are not passing product acceptance checks.

Automated IME geometry checks do not establish real Microsoft Pinyin interaction;
the existing human IME records remain applicable and Linux IME remains deferred.
Hosted-runner resource measurements are not performance acceptance thresholds.

## Dependencies and reproducibility

Linux CI and packaged binaries target Ubuntu 26.04+ with GCC 15 and its matching
`gcov-15`. The retained SDK capability test also compiles, links and executes
C++23 `std::print`/`std::println`, checking exact formatted UTF-8 bytes.
Native analysis and fuzzing use Clang 21 against the same Linux environment.
The runtime baseline increase from Ubuntu 24.04 was explicitly approved; existing
test registration order, assertions, timeouts, parallelism and quality gates stay
in force. CI and native GTK drivers use Python 3.14, matching Ubuntu 26.04's
compiled GI bindings; after a system Python upgrade, use a fresh CMake build and recreate
any virtual environment rather than reusing cached interpreter paths.
GUI tests use Ubuntu 26.04's Weston 14 and Openbox packages. No separately built
Weston or older distribution runtime libraries are selected.
The native input driver uses upstream xdotool 4.20260303.1, built from a fixed
release commit with its own libxdo embedded. Ubuntu's older xdotool misreads
xkeyboard-config 2.46 function-key levels and injects an extra Alt modifier
([upstream #491](https://github.com/jordansissel/xdotool/issues/491)). The original
key names, XTEST delivery, behavior assertions and deadlines are retained.
The regional-language fixture reads glibc's `%x` in an independent Python process
under its private `LOCPATH`. Ubuntu 26.04's uutils `date` ignores that locale for
this format ([upstream #10284](https://github.com/uutils/coreutils/issues/10284)).
The existing before/after time boundary and exact output comparisons remain.
The same independent glibc client checks the private locale's comma decimal
separator after loading all locale categories. The runner's confined `locale`
utility cannot read that private directory; its system policy remains unchanged.

- Actions are pinned to full commit SHAs, annotated with their major versions.
- actionlint 1.7.12 predates the official `ubuntu-26.04` runner label. Its
  configuration lists that exact additional label; runner-label checking remains
  enabled for all workflows and all other labels.
- Linux provisioning has three profiles: `build` for CodeQL, `tidy` for native
  clang-tidy, and `gui` for the build/test matrix. GUI display tools and fonts
  are installed by the `gui` profile, also used by coverage. The analyzer is
  installed by `tidy` and by `gui` for native analyzer contract tests.
- Linux jobs use Ubuntu's official HTTPS archive in place of the Azure archive
  mirror or the runner's `/etc/apt/apt-mirrors.txt` mirror-list URI in legacy
  `.list` and Deb822 `.sources` files. Suites, components and signing keys are
  preserved. APT uses finite connection/read timeouts and retries. Downloaded `.deb`
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
  **3.3.4#1**. Only core features are requested, matching the prepared development
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
  Version 6 also removes unused Windows GIF/PCX/TGA/IFF/PNM handlers,
  animation/date/time controls, wx's stock find/replace and wizard/splash
  dialogs, ActiveX/joystick, SearchCtrl/RichTooltip, SVG file export,
  PostScript/AFM and ZIP/TAR archive facilities. Native Windows printing,
  PNG/ICO/XPM decoding, NanoSVG artwork and zlib remain enabled. The application
  icon and every toolbar image, including currently unused slots and all five
  DPI variants, remain complete. Windows loads its existing icon resource group;
  other platforms retain the embedded ICO byte array.
  The wxMSW overlay also locks the pending-watch removal vector shared by the
  UI thread and IOCP worker, preventing concurrent reallocation/erase from
  destroying the same watch twice. `native_file_watcher_lifetime` exercises
  repeated Refresh, pending Stop, restart and destruction during directory writes.
- Linux explicitly requests GTK3's Wayland feature alongside its X11 backend.
  Installing the system GTK development package does not enable Wayland inside
  the separately built vcpkg GTK. Configure records and checks the actual SDK
  backend macros before compiling; the full native suites still exercise both.
- The GTK3 overlay in `.github/ports` preserves the pinned 3.24.52 archive and
  build patches, adding native pathbar button-query cancellation and linking
  configured print backends into static Linux GTK. GTK’s native Print to File
  stays usable without modules from the build machine. Explicit vcpkg
  installation selects this overlay; its files participate in the existing cache
  key, and vcpkg's package ABI validates restored binaries. Local dependency builds
  can select the same overlay with `--overlay-ports=<repository>/.github/ports`.
- Host-only GNU gettext 0.23.2 tools are pinned by the repository overlay, matching Ubuntu 26.04. CI
  verifies their versions and extraction/format/release coverage before application
  builds; generated app and locked wxWidgets catalogs accompany every artifact.
  See [localization.md](localization.md) for maintenance and resource layout.
- CI-only triplets build Release dependencies, avoiding unused Debug builds.
  Both use static third-party libraries and dynamic C/C++ runtimes. Windows
  keeps `VCPKG_CRT_LINKAGE=dynamic` and MSVC `/MD`; normal Windows development
  uses `x64-windows-static-md`. Existing dynamic SDKs need a fresh static install
  and a new application build directory. Linux keeps its existing static
  wxWidgets/GTK profile and dynamic native system interfaces, including D-Bus.
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

Python analysis targets the workflow's Python 3.14. Python files under `tests/`
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
of executed application lines. The existing private-session sharder isolates each
concurrent Linux coverage shard under `<build>/gcov-shards/<backend>-shard-<N>`
via `GCOV_PREFIX` and `GCOV_PREFIX_STRIP`, eliminating cross-shard `libgcov`
profile-file lock contention while keeping per-shard counter merging intact.
The byte-exact large-file language test has a 120-second guard in coverage builds
and 60 seconds in ordinary builds. The instrumented scenario completed all checks
in 88.50 seconds under controlled CPU contention. Supported Linux CMake versions
use SIGQUIT on timeout so the existing crash collector can preserve a core/stack.

`collect_coverage.py` runs GCC 15's `gcov` for every notes file paired with any
direct or per-shard counter files, including translation units without counters. It preserves unmodified maintained `src/`
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

Release and MinSizeRel enable `WXNOTEPAD_ENABLE_SIZE_OPTIMIZATIONS` by default.
MSVC packages global data with `/Gw` and uses `/OPT:REF,ICF /INCREMENTAL:NO`.
Linux GCC/Clang compile project libraries into function/data sections, then use
`--gc-sections` and `-z pack-relative-relocs` when the actual linker supports
them. The Linux runtime baseline is Ubuntu 26.04+, with DT_RELR-capable glibc.
Application and native-regression links use the same settings. Debug, coverage
and fuzz-only builds retain their existing options; the compiler's speed
optimization level and exception/unwind support remain in place.

Set `-DWXNOTEPAD_ENABLE_SIZE_OPTIMIZATIONS=OFF` for an otherwise identical
comparison build. `-DWXNOTEPAD_ENABLE_IPO=ON` separately enables checked
interprocedural optimization for the application and its four project libraries
in Release/MinSizeRel. It defaults to OFF because the full native suite links
many independent executables against those archives. For a focused application
comparison, combine it with `-DBUILD_TESTING=OFF` in a separate build directory.
IPO and coverage require separate build trees.

The build evidence retains `application-build-commands.txt`, including the
final link command, and `binary-size.json`. The size report inventories actual
distributed files, executable sections and symbols/relocations. It also verifies
that all five complete toolbar PNG strips and every original ICO image payload
occur in the packaged application, so unused artwork remains available for
future features. Native appearance tests separately load all eight icon sizes.
Run from a working directory that contains the report files. `--output` and
the optional `--baseline-report` must resolve inside that directory, including
any symlink destinations. Artifact and source inputs can be read from other
directories. To inspect an existing package, use:

```text
python -X utf8 .github/scripts/binary_size.py --artifact-dir /path/to/artifact --source-root . --output .ci/reports/binary-size.json
```

Add `--baseline-report .ci/reports/binary-size-before.json` to compare against
a report from the same platform.

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
The Windows original Character Info fixture uses the same monitor with a
75-second capture under its existing 90-second CTest guard. Its evidence also
retains the last flushed fixture phase, the dump, and the matching binary and
any PDB. The Windows Base64 fixture also uses this existing monitor, capturing
at 45 seconds under its original 60-second guard. Its flushed phases distinguish
the 498 captured conversions, command states, native ANSI bytes, character
information and frame cleanup. The evidence retains its matching binary and
any PDB; all original samples and assertions remain in force. Windows clipboard
errors in both fixtures go to stderr so each reports a failed
assertion without opening an additional modal error dialog.
The Windows-only `native_base64_clipboard_error` regression holds the native
clipboard at the actual Character Info copy boundary and requires the original
failing assertion, native error text and child exit 1 within ten seconds. Its
own 20-second CTest guard leaves existing test deadlines and order unchanged.

The JSON native fixture waits for GTK's initial folder load before selecting a
file, so that load cannot replace the requested selection. On Linux with CMake
3.27 or newer, its unchanged 60-second timeout sends SIGQUIT with a five-second
dump grace period.
The existing Linux core collector retains its executable and GDB thread stacks.
The ordinary and coverage suites each retain this registered case. The separate
**Wayland JSON investigation** workflow (`json-investigation.yml`) manually
builds only `syntax_json` and repeats that same case up to 20 times in a private
Wayland session, stopping at the first failure. It retains the original timeout,
SIGQUIT grace period, JUnit, display logs, core/stack evidence and matching
executable. It does not run automatically on pushes or pull requests. Dispatch it
against the desired revision when a focused repetition is needed:

```text
gh workflow run json-investigation.yml --ref codex/branch-to-investigate
```

The search-seeding fixture uses the same SIGQUIT mechanism under its original
60-second deadline. Its flushed monotonic stage logs distinguish the 1 MiB
selection boundary, over-limit selection, clipboard fallback and window cleanup.

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
or Ubuntu installer. Windows application dependencies are statically linked;
the Microsoft C/C++ runtime remains dynamic. Linux
output includes the shared D-Bus interface required by its port and a README describing its
Ubuntu 26.04 desktop/runtime requirement. Distribution packaging remains a
separate task.

Linux packaging also copies the selected SDK's `libexec/gio-launch-desktop`,
which GLib uses to launch desktop entries. The vcpkg GLib binary's compiled
helper prefix names its disposable package staging tree, so it cannot locate
the installed or relocated helper by that prefix after dependency cleanup.
The canonical Linux test runner sets `GIO_LAUNCH_DESKTOP` to the selected
CMake SDK's helper. Packaged checks instead select the artifact's own helper.
The artifact's `README.txt` supplies the explicit `GIO_LAUNCH_DESKTOP` and
`LD_LIBRARY_PATH` command; running the bare ELF does not set these paths.

Linux packaging strips only the distribution copies of the executable, helper and
bundled shared libraries. Their matching separate debug files are retained under
`.ci/symbols/` in the evidence artifact, with GNU debug links in the distributed
binaries. Copy `.ci/symbols/.debug/` beside the executable and `.ci/symbols/lib/.debug/`
inside its `lib/` directory for GDB to find them; the helper's symbols use
`libexec/.debug/`. Both ELF dependency inventories are retained, and SDK libraries
needed by either binary are included. The ordinary unstripped
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
to clang-tidy-21; use `--executable` for another installed native analyzer. Keep
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

The Escape minimize/restore fixture requires the Ubuntu `openbox` test package
on X11. Its test wrapper starts an owned Openbox with a private configuration
inside the existing authenticated Xvfb session and checks native WM readiness
within three seconds. Only the new Escape cases use this wrapper; existing
session concurrency and case deadlines are unchanged. Wayland uses the retained
private Weston compositor. Openbox is not an application runtime dependency.

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
SDK artifacts fail on both platforms. Windows runtime output rejects shared
application dependencies while allowing dynamic Microsoft C/C++ runtime DLLs.
Linux output likewise rejects shared application libraries; the forced-shared
D-Bus interface and compiler runtimes remain allowed. The native SDK capability
test also rejects a DLL wxWidgets build or a static MSVC runtime. The Linux GTK
dependency profile is unchanged. Omitting `--artifact-dir` explicitly checks only the SDK and records
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
