# GitHub Actions

Actions supplement the local build, relevant tests, canonical clang-format and
clang-tidy checks used during implementation. They verify fresh hosted environments
and retain shareable results; they do not replace local verification.

## Workflows

| Workflow | Trigger | Work |
| --- | --- | --- |
| CI | Pull requests, pushes to main/codex branches, manual | Windows 2025/MSVC and Ubuntu 24.04/GCC 13 builds, all registered CTest tests, development build output |
| Quality | Same | clang-format 23.1.2, Python syntax, actionlint 1.7.12, native Linux clang-tidy 18 |
| CodeQL | Same | Traced C++ application build and Actions configuration analysis, with results in GitHub Security |

The Linux CI job builds once, then runs X11 and Wayland in separate private
Xvfb/dbus sessions. The existing Weston focus helper supplies the Wayland session.
Tests run serially because native dialogs and clipboard ownership are shared
within a display session. Wayland still runs if X11 tests fail after a successful
build. Windows runs its full registered suite on the hosted VM: the four tests
excluded on the development machine's shared desktop are not silently excluded.

Automated IME geometry checks do not establish real Microsoft Pinyin interaction;
the existing human IME records remain applicable and Linux IME remains deferred.
Hosted-runner resource measurements are not performance acceptance thresholds.

## Dependencies and reproducibility

- Actions are pinned to full commit SHAs, annotated with their major versions.
- The vcpkg checkout uses the manifest's builtin baseline and wxWidgets override
  **3.3.3#1**. Only core features are requested, matching the prepared development
  dependencies; unused sound/SDL2 and debug-support features are not requested.
- Linux explicitly requests GTK3's Wayland feature alongside its X11 backend.
  Installing the system GTK development package does not enable Wayland inside
  the separately built vcpkg GTK. Configure records and checks the actual SDK
  backend macros before compiling; the full native suites still exercise both.
- CI-only triplets build Release dependencies, avoiding unused Debug builds.
  Normal development triplets and the existing local installations are unchanged.
- Windows uses the installed Visual Studio C++ developer shell and Ninja, matching
  vcpkg's toolchain selection rather than hard-coding a Visual Studio generator.
  CMake arguments cross the PowerShell boundary as a JSON array so drive-letter
  paths, spaces and literal characters survive `pwsh -File` parameter parsing.
- Binary packages and downloads are cached by OS, architecture, runner image and
  manifest/triplet/provisioning hashes. vcpkg's own ABI keys validate restored packages. Cached installed
  trees or application build directories are not reused between checkouts.
- The dependency cache is saved after successful installation, before GUI tests.
  A later test failure therefore does not discard an expensive successful build.
- Core preparation goes through the existing build tool and pinned archive/patch
  checks, with command-local LF settings for archive and patch application so a
  runner's Git autocrlf configuration cannot change prepared bytes. A real-Git regression test runs on
  both hosts before dependency installation and also checks corrupted-archive rejection.
  Windows also checks actual CMake argument forwarding and failure propagation
  through the installed developer shell before provisioning dependencies.
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

## Evidence and build output

Evidence artifacts retain JUnit XML, complete CTest logs, Weston logs, configure
logs, source provenance and dependency build logs, including on failed jobs.
clang-tidy exports per-file diagnostics and a JSON summary. Evidence retention is
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

Newer runs cancel older runs for the same workflow/ref. Workflow files can run
from a pushed integration branch before they reach main; the Actions manual-run
UI is registered from the default branch after merge. No schedule is installed.

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
