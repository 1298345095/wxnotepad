# Architecture restructuring validation

The restructuring was based on the accepted plan at `6cd13eb` and implemented in
`71df6e0e042813b1ba732e41f23f0226c2eea378` on `codex/refactor-architecture`.
Later delivery changes only update documentation. No editor feature, runtime
dependency or GoogleTest dependency was added.

## Resulting structure

- `app`: native frame, Find/Replace dialog, startup and optional process observer.
- `document`: file transactions, encoding/BOM, document-local language choice,
  and existing encoding implementations.
- `search`: the real core's matching/replacement workflow behind `SearchSession`,
  with structured outcomes and revision-based match/progress state.
- `syntax`: a static catalog shared by menus, extension lookup and installation,
  with the original generated keyword/style data.
- `editor`: the existing wx host and original platform implementation.

Text, selections, styling/folding, history and the save point still belong to the
original core. Search UI controls no longer implement matching or replacement.
The document session applies language transitions only after successful file
operations. The new `editor_runtime`, `editor_features` and `app_shell` static
targets follow these responsibilities; original core objects are included in the
runtime archive to keep host/platform symbols together.

Native IME, clipboard and rendering code was retained. The private file I/O
mechanics remain in `DocumentSession`; the plan made further extraction
conditional on a useful implementation seam. Future settings, themes, languages
and large-file policy are described in [the architecture document](../architecture.md).

## Behavioral validation

| Environment | Existing and new checks | Result |
| --- | --- | --- |
| Windows, MSVC 14.51, wxWidgets 3.3.3#1 | Related CTest suite | 31/31 |
| Ubuntu 24.04, GCC 13.3, private X11 | Complete CTest suite | 33/33 |
| Ubuntu 24.04, GCC 13.3, private Wayland | Complete CTest suite | 33/33 |
| Windows, X11, Wayland | Independent actual-app observation protocol | Passed on all three |

The existing native cases cover search/replace, zero-width progress, capture
revalidation, byte-identical replacement, undo/redo, language choice, save/open
transitions, folding, input, font/DPI and file bytes. They were retained throughout
extraction. Intermediate Windows checks also passed after the search extraction
(9/9), UI extraction (10/10), language-state/catalog extraction (14/14) and source/
build organization (30/30).

`feature_sessions.cpp` adds a focused check of the new interface contract. It
uses a real editor without linking `app_shell`: structured search outcomes,
zero-width request invalidation, no-op redo/save-point preservation, and document
language transitions with independently read saved bytes. This verifies that
the modules work without creating the frame or its Find/Replace dialog.

`tests/resource_process.py` launches the actual application using `--observe` and
`--startup-file`, checks visible state, delivers a native edit, requests Save and
independently checks the disk bytes. This exercises the extracted observer.

Windows excludes `system_clipboard`, `gbk_native_choices`, `open_new_native` and
`save_as_native`, which require shared-desktop interaction. The complete isolated
Linux suites include them. No skip markers were found in the final CTest case
logs. Native Windows IME geometry remains in the suite; Linux IME remains deferred.
No screenshots were inspected. Private Weston shutdown diagnostics are retained
alongside successful test/process results.

## Source and build reproducibility

Both platforms build offline from the pinned original source archive. The
original revision and portability patch are unchanged. All three generated
headers compare byte-for-byte with the pre-refactor revision after moving and
running the updated generators against the pinned original checkout.

The actual Windows link rules confirm that `feature_sessions` links
`editor_features` and `editor_runtime`, without `app_shell`. The application links
the shell; the runtime archive contains the original core object files. The same
target structure built and linked with GCC on Linux.

## Formatting and static analysis

clang-format 23.1.2 passed `--dry-run --Werror` for 66 hand-maintained source/header
files. Generated data is separately checked for exact byte equivalence.

Each native clang-tidy run checked all 12 production translation units and the
new interface test. All 13 analyzer processes on each platform exited zero.
The selected checks were `clang-analyzer-*`, `bugprone-*`, `performance-*` and
`portability-*`.

| Analyzer | Production diagnostics | Including the new test |
| --- | --- | --- |
| Windows LLVM 23.1.2 | 80 | 80 |
| Linux LLVM 18.1.3 | 47 | 47 |

The new enum-width, catalog-padding and test cleanup diagnostics were fixed.
Compared with phase 3, production diagnostic counts by check are unchanged on
Linux; Windows adds four `portability-avoid-pragma-once` findings from the new
headers, which follow the existing header style. Remaining native wx flags,
coordinate conversions, upstream-interface enum conventions and known allocation/
exception findings retain the prior classification. This is not a warning-free
claim; raw locations, messages and analyzer commands are preserved.

## Standards

The independent Standards review found no actionable documented-standard breach
or newly introduced baseline smell in `6cd13eb...71df6e0`. It checked the repo
instructions, domain terminology/ADRs and the module-design principles, excluding
issues already enforced by tooling. Search owns real behavior; the syntax catalog
centralizes language variation; observation has an explicit lifecycle.

## Spec

The independent Spec review found no actionable committed-source finding against
the accepted architecture plan. Search progress/history, dialog wiring, document
language transitions, catalog data and the build separation matched the plan.
Conditional native-helper/file-I/O extraction was appropriately retained in place.

Review totals: Standards 0; Spec 0. Neither axis has an outstanding issue.

## Evidence and remaining scope

Machine-local archive: `E:/NewNotepad4/wxnotepad-evidence/architecture-refactor/`.
It contains implementation scripts, intermediate/final logs, CTest case outputs,
analyzer diagnostics, review reports, build provenance, source archives, a Git
bundle, runnable artifacts and a verified checksum manifest.

The active checkout and its `build-refactor` output remain available. Linux builds
and checks ran on the native filesystem in
`/home/ctrls/.cache/wxnotepad-architecture-refactor`; the final cache is retained.
No disposable worktree was created for this restructuring. Earlier phase evidence
and retained builds were not removed.

The existing large-file styling memory cost was not optimized in this change.
No new performance threshold, settings behavior or extra language was introduced.
