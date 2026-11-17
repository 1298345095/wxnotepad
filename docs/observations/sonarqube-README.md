# SonarQube analysis

Current project: `1298345095_wxnotepad`, default branch `main`.

Commit `66cbd148` changed the configured project key from
`1298345095_wxnotepad2` to `1298345095_wxnotepad`. The November 4–10
baselines and dispositions below describe the earlier project and the application
revisions named in each section. Current-project findings are reviewed separately
against their current issue keys; the continuation record appears at the end.

Latest review: [PR #224 analysis follow-up](#pr-224-analysis-follow-up),
on [`codex/sonarcloud-enum-cleanup`](https://github.com/1298345095/wxnotepad/tree/codex/sonarcloud-enum-cleanup).
[PR #223](https://github.com/1298345095/wxnotepad/pull/223) was merged into main
at `395ad59521dff2e7a5fb1c660188d73ea2269726` before this review.

The 2026-11-04 MCP baseline at application revision
`7a3da91cadaaf93accab530cacd178a76aecc494` contained 863 open issues.
Of these, 48 affected security or reliability. The quality gate reported new-code
security and reliability ratings of E and 5.3% duplicated lines (limit: 3%).
Raw MCP responses and local validation logs are retained outside the checkout in
`E:/NewNotepad4/wxnotepad-evidence/sonarqube-2026-11-04/`.

## Code changes

- Assign the Find All anchor count directly to its `std::optional` and make the
  presence check explicit. The existing initialization and original anchor-count
  behavior are preserved.
- Put file fixtures beneath a shared, uniquely named private test root. Directory
  creation rejects existing paths; Unix permissions are 0700. Existing canonical
  Windows paths and per-test cleanup remain supported.
- Reuse file readers, filename conversion, editor-text reads, append commands and
  visible syntax-pass checks across fixtures. Independent test actions and expected
  document bytes remain in their original test processes. Named fixture paths
  share the private process root rather than composing redundant PID suffixes.
- Give the private Wayland driver a fixed geometry response inside its own runtime
  directory. The fixture requests geometry without supplying a write destination.
- Use explicit measurement checks that remain active with Python optimization.
  Preserve failed-row recording and the independent, full saved-byte comparison.
- Require HTTPS for both the actionlint download and redirects.

## Analysis scope

CI-based analysis reads `sonar-project.properties` and the existing Linux build's
compilation database. Application sources, C++ tests, developer tools and workflows
remain in `sonar.sources`. Python fixtures are classified with `sonar.tests` and a
Python-only inclusion pattern, which also excludes them from main source scope.
CFamily disregards `sonar.tests`, so C++ fixtures stay in source scope to retain
analysis. Python targets the CI interpreter version, 3.12. Generated language
scheme tables matching `src/syntax/generated/*_scheme_data.h` (19 files at the
November 16 baseline) and the frozen original observations in
`tests/auto_insertion_reference.h` and `tests/auto_close_tags_reference.h` are
excluded from copy/paste metrics. Their contents and ordering come from the
pinned upstream schemes or independent original observations. Other code
analysis remains enabled for those files. Quality-gate thresholds and rule
profiles are unchanged.

Test fixtures are excluded from coverage metrics through `sonar.coverage.exclusions`,
while their code analysis remains enabled. Application coverage scope is retained.

Configuration and cutover: [GitHub Actions](../ci.md#sonarcloud).
Reference: [SonarQube Cloud compilation-database analysis](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/languages/c-family/running-the-analysis).
See also [CFamily test-file analysis](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/languages/c-family/customizing-the-analysis).

Native coverage is imported from gcovr's merged generic XML. Main analysis at
`7f3a0cf` confirmed that direct per-TU gcov import lost complementary shared-header
branches: `document_session.h:69` reported 3/4 despite a raw union of 4/4.
`column_input.h:21` is a repeated-outcome control and correctly remains 3/4.
The collector keeps the original reports and uses pinned `gcovr==8.8` to merge
them before generic import. Coverage gates and source scope are retained.

The approved native coverage policy excludes GCC-marked throw edges via gcovr's
`--exclude-throw-branches`. A same-counter comparison retained 73 source files
and 6969 executable lines while removing 4027 exceptional outcomes, including
one covered outcome. Combined native coverage changed from 73.18% to 90.32%.
These are whole-native-report measurements; main's new-code gate is measured
separately. Ordinary conditions, uncovered handler lines, all raw reports and
the existing source scope and 80% threshold are retained.

## CLI finding review

The command runners intentionally accept a local executable, repository, build
directory or output directory from the developer. Their subprocess calls use
argument arrays with shell execution disabled. Formatter inputs come from tracked
`src/` and `tests/` paths; analyzer inputs are resolved and restricted to those
directories. Generator revisions and Git object paths are pinned or come from the
pinned Git tree. The resource report destination is the explicit CLI output
directory, created before reading app state; observed state is serialized as data.

These data flows must be considered when reviewing injection findings. Preserve
the documented CLI contracts when resolving a false positive, and attach the
specific source-to-sink explanation to each reviewed finding.

The ten reported CLI findings received individual false-positive dispositions,
each with its reviewed source-to-sink explanation. Mutation responses are retained
with the raw MCP baseline.

The `std::print` suggestions affecting this change were reviewed against the
supported Ubuntu 24.04/GCC 13 baseline. Its libstdc++ lacks `<print>`; a direct
C++23 compilation records the missing-header error. The portable `fprintf`
diagnostics remain, with finding-specific portability dispositions.

## Native peer geometry readiness

Hosted Wayland validation exposed a separate test-IPC race: the peer publishes
`ready` before its 30 ms timer first writes its widget offset. A mapped compositor
surface therefore does not imply that both geometry inputs are available. The
consumer now waits for the compositor response and the peer offset, and the peer
closes and atomically replaces coordinate snapshots before readers can observe
them.

`native_peer_geometry_ready` starts the real peer with delayed coordinate
publication and reaches the same geometry consumer without running the preceding
drag scenarios. It reproduced the missing-offset failure in three runs before the
fix and passed three runs afterward. This Wayland-specific check explicitly skips
on X11; the existing full drag test continues to run on both backends. Raw red/green
logs and the original failed CI artifact are retained with the other evidence.

## wxWidgets test integration

In `tests/search_backslashes.cpp`, S110 is accepted for the `BackslashApp`
integration: the fixture adds one class directly above `wxApp`, while the
remaining reported inheritance depth belongs to wxWidgets. The standard
`wxIMPLEMENT_APP` entry point uses its native initialization and process-result
hooks.

The top-level `wxFrame` allocation is an S5025 false positive. Both test outcomes
call `Destroy()`. In the pinned wxWidgets 3.3.3 source,
`wxTopLevelWindowBase::Destroy` queues deferred deletion,
`wxAppConsoleBase::DeletePendingObjects` deletes queued objects, and
`wxAppBase::DeleteAllTLWs` deletes remaining top-level windows during cleanup.
This frame follows the framework's ownership contract. Each disposition has an
issue-specific source explanation and independent Standards and Spec review.

## Main gate review after compilation-database cutover

At `921159a`, main's wider new-code period reports eight reliability and six
security issues. The two S6427 findings in `EditorControl::SwapClipboard` are
resolved by assigning directly to the optional. Its existing nonempty check,
line-ending conversion, native encoding conversion and selection rules remain.

The other twelve findings have these specific false-positive explanations:

| Rule | Location | Source evidence |
| --- | --- | --- |
| S3584 | `tests/path_text.cpp`, `PathTextApp::OnInit` | The reported allocation is `wxLogStderr`, stored by `wxLog::SetActiveTarget`. |
| S3584 | `tests/stream_drag.cpp`, `DragApp::OnInit` | The new active logger is retained; the returned previous logger is deleted. |
| S3584 | `tests/clipboard_smoke.cpp`, `ClipboardApp::OnInit` | The reported allocation is the retained active `wxLogStderr`. |
| S2259 | `sort_lines_dialog.cpp`, initial radio choice | Every element is assigned by the fixed-size construction loop; the selected index is 0–3 (`Descending` is 1). |
| S2259 | `sort_lines_dialog.cpp`, column checkbox | The seven-element construction loop initializes index 6 before `Enable`. |
| S2259 | `align_lines_dialog.cpp`, accepted choice | All five radio pointers are initialized before the modal dialog and the result loop. |
| S5145 | `editor_test_helpers.h`, expected anchor | A fixed `%lld` conversion formats an integer position. |
| S5145 | `editor_test_helpers.h`, expected caret | A fixed `%lld` conversion formats an integer position. |
| S5145 | `url_conversion.cpp`, fixture name | The source is the repository's compile-time `URL_REFERENCE_FIXTURES` TSV, used by the native test. |
| S5145 | `special_insert.cpp`, fixture row | The source is the repository's compile-time `SPECIAL_INSERT_STATES` TSV. |
| S5145 | `special_insert.cpp`, expected state | This is a substring of that same fixed fixture row. |
| S5145 | `insert_tag.cpp`, scanner row | The source is the repository's compile-time `TAG_REFERENCE_FIXTURES` TSV. |

For all three logger findings, the pinned wxWidgets 3.3.3 implementation stores
the new pointer in `wxLog::ms_pLogger` (`src/common/log.cpp:531`).
`wxAppBase::CleanUp` deletes it via `delete wxLog::SetActiveTarget(nullptr)`
(`src/common/appcmn.cpp:164`). The analyzer's issue flows identify these new logger
allocations, rather than an old target or a frame. The dialog findings miss
elements initialized by loops; ordinary throwing `new` cannot return a null
pointer on the continuing path. The diagnostic findings have either numeric
formatting or repository-owned fixture sources.

Issue-specific transitions and review receipts are retained with the local
`sonar-gate-fixes-2026-11-07` evidence. No rule profile or gate threshold is changed.

## Completion integration ownership review

The completion integration's S5025 reports follow the same native ownership
contract. `ShowAutoCompletionSettingsDialog` transfers its nested sizers through
`Add` and `SetSizerAndFit`; its controls are children of the stack-owned dialog.
`WxListBox::Create` records the popup in `ListBox::wid`, parents the native list
to that popup and transfers its layout with `SetSizer`. Core cancellation and
native parent destruction release this hierarchy. Native regression frames are
wxApp-owned top-level windows and call `Destroy` after both successful and caught
failure paths; their focus-target buttons are child windows. Independent smart
pointers must not also own these objects. Each reported allocation received its
own source-to-owner explanation and false-positive disposition.

The `update_schemes.py` output directory is an explicit local developer CLI
destination, following the existing generator contract. Its `--schemes` values
come from argparse's fixed registry choices, so generated basenames cannot
contain caller-supplied separators or parent components. Original input paths
are fixed Git object names at the pinned revision. The reported path-traversal
finding assumes a trusted caller selected an unintended output destination;
it does not identify untrusted project data reaching that destination. Its
individual disposition preserves this intentional CLI behavior.

Raw review responses and native regression logs are retained in the local
`completion-languages-2026-11-10` evidence directory.

## 2026-11-16 continuation

The current-project `main` baseline at `66cbd148` contained 4,367 open issues.
The SonarCloud HIGH severity query returned 18 findings; the combined security
and reliability query returned four: one S8417 and three S5813 findings. These
counts describe that analysis revision before the continuation's changes and
dispositions. The cleanup branch started this continuation at `ac6528a5`.

The historical S6427, S3584, S2259 and S5145 findings have no open matches in
this baseline. The previously accepted `BackslashApp` S110 is also absent from
the open results. The project-key change therefore does not establish that
earlier reviewed findings reopened. Current dispositions use the exact issue
keys and source locations from the new baseline.

### Changes on the cleanup branch

| Commit | Reviewed change |
| --- | --- |
| `3ac44a1` | Correct the always-true lexer condition that rejected completion after embedded JavaScript/CSS escapes. Add native cases for embedded and standalone JavaScript, escaped backslashes, committed bytes, caret placement and one-action Undo. |
| `fec947f` | Extract Linux runtime bundling, centralize the build-record filename and simplify Python dictionary construction. Add distribution-content, symbol-separation and ASCII language-code checks. |
| `ac6528a` | Replace the completion tree's fixed 32-entry insertion and traversal paths with growing vectors. The existing reproduction used 200,000 sorted candidates to expose the out-of-bounds write; the native regression checks the candidate endpoints and unchanged document history. |
| `cd4e171` | Address ten reviewed measurement findings: use explicit Pango/Cairo deleter addresses, use default sequential consistency for snapshot IDs, group fonts with their expected metrics and separate native references, diagnostics and worker execution. Preserve the four-worker, 64-iteration checks and their independent drawing oracle. |
| `7b497c1` | Address twenty reviewed native-test findings: separate file-chooser transitions and interaction scenarios, make pixel-coordinate conversions explicit, remove unused helper parameters and mark Windows-only parameter uses. Preserve the native events, retries, deadlines, assertions and cleanup paths. |
| `21cd0cd` | Fix an experimentally reproduced allocation-failure crash in completion. Let allocation exceptions reach the completion entry boundary, report `BadAlloc` and cancel the popup before returning through native notifications. Publish buffer bounds only after new storage is owned, allowing failed growth to be retried safely. |
| `bc6ea68` | Correct two embedded JavaScript escape-style expectations exposed by the native X11 run. Replace the test's hard-coded `+ 40` offsets with `js_style(SCE_JS_ESCAPECHAR)`; the pinned core's mapping uses an offset of 47. |
| `5de95ab` | Move the replacement allocation hooks into their own translation unit, encapsulate injection state in a function-local `thread_local` object, prohibit copying or moving the failure guard, and use the standard `operator new` entry point for raw allocation. Preserve all seven native allocation-failure scenarios. |

Commit `21cd0cd` adds `tests/completion_allocation.cpp`, which injects failures
into the actual candidate-storage classes and exercises native character entry,
active-popup cancellation, Undo/Redo and subsequent completion. The source
changes through `bc6ea68` were analyzed in PR #219. The allocator follow-up at
`5de95ab` subsequently passed its own hosted validation and SonarCloud analysis,
as recorded below.

### Verified issue-specific dispositions

All 367 reviewed `main` false-positive transitions returned successful MCP
responses. Three queries using the exact issue keys and the FALSE_POSITIVE
status filter verified every transition in the `main` context. This reduced
the current project's `main` OPEN count from 4,367 to 4,000. The total comprises
the 30 findings below and the 337 compiler-portability findings described in
the next section. Each transition includes its own source explanation.

The completed [main disposition ledger](sonarqube-2026-11-16-dispositions.json)
records all 367 keys, rules, locations, submitted explanations, successful MCP
receipts and verified status.

| Rule | Count | Evidence supporting the disposition |
| --- | --- | --- |
| S5813 | 3 | `tests/syntax_markup.cpp` passes one of three local literal tokens to `strlen`. The two `block_comments.cpp` operands come from the constexpr syntax registry or fixed embedded-language delimiter pairs; the caller checks the enabled opening delimiter. Each operand is a NUL-terminated literal with static storage. |
| S5025 | 2 | The frames in `measurement_threads.cpp` and `wrap_resize.cpp` follow wxWidgets top-level ownership. Their callbacks call `Destroy()` after success or caught test failure. Pinned wxWidgets 3.3.3 queues and deletes pending windows and cleans up remaining top-level windows during application shutdown. |
| Python S6353 | 1 | `localization.py` deliberately accepts ASCII release-language identifiers. Python's Unicode `\w` would additionally accept `ené`, `en中文` and `en１２`. The existing six-case identifier test preserves the intended accepted set. |
| S5276 | 24 | The reviewed completion calls read 15 `GetCharAt`, six `GetStyleIndexAt` and three `GetCharacterAndWidth` results. In pinned core `89b8b36`, the first two return `unsigned char` and the third returns `int`. Their pointer-sized message transport preserves those values when converted back to `int`; document positions and the separate width output retain their position types. |

The ownership review checked wxWidgets 3.3.3
[`wxTopLevelWindowBase::Destroy`](https://github.com/wxWidgets/wxWidgets/blob/v3.3.3/src/common/toplvcmn.cpp),
[`wxAppConsoleBase::DeletePendingObjects`](https://github.com/wxWidgets/wxWidgets/blob/v3.3.3/src/common/appbase.cpp)
and [`wxAppBase::DeleteAllTLWs`](https://github.com/wxWidgets/wxWidgets/blob/v3.3.3/src/common/appcmn.cpp).
The regex review follows Python 3.12's documented
[`str` pattern and ASCII matching behavior](https://docs.python.org/3.12/library/re.html#regular-expression-syntax).

PR #219 at `bc6ea68` had six additional false positives. Each received a
successful MCP response and was verified by exact issue key with the
FALSE_POSITIVE filter in the same PR context. Their separate
[PR disposition ledger](sonarqube-2026-11-16-pr219-dispositions.json) records
all six verified entries.

| Rule | PR count | Evidence supporting the PR disposition |
| --- | --- | --- |
| S1231 | 1 | The test executable's replacement allocation functions use `malloc` for raw storage and matching `free` calls in their delete replacements. Object construction and destruction remain with the new/delete expressions. Replacing that storage call with `new` would recurse into the replacement allocator. |
| S5025 | 1 | The allocation fixture's `EditorFrame` follows the same wxWidgets top-level ownership and deferred `Destroy()` contract reviewed above. |
| S1481 | 2 | Each lambda passes `root.data()` from a local constexpr `std::array` to `WordList::Init`. The array's storage is odr-used, so its reference capture is required; the compiler check succeeds with the capture and rejects its removal. |
| S6494 | 2 | The new fixture's shared diagnostics still target the supported GCC 13 baseline, which lacks `std::print`. |

The C++ [replacement allocation requirements](https://eel.is/c++draft/new.delete.single)
allow an ordinary replacement `operator new` to provide suitably aligned raw
storage or throw `std::bad_alloc`. The fixture normalizes zero-size requests and
retains the `new_handler` retry protocol. GCC 13's
[`new_op.cc`](https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.4.0/libstdc%2B%2B-v3/libsupc%2B%2B/new_op.cc)
and [`del_op.cc`](https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.4.0/libstdc%2B%2B-v3/libsupc%2B%2B/del_op.cc)
also implement this raw-storage layer with `malloc` and `free`.

### Formatting recommendations and compiler support

The S6494 baseline contains 337 `std::print` recommendations across 144 files:
268 `fprintf` calls and 69 `printf` calls. Each reported call was checked against
the baseline source. Platform-conditional cases select GTK/Linux or non-MSW
paths. The repository's dependency action explicitly installs GCC 13 and exports
`CC=gcc-13` and `CXX=g++-13`.

GCC's [libstdc++ implementation table](https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html)
records P2093R14 formatted output as first available in GCC 14.1, and the
[GCC 14 release notes](https://gcc.gnu.org/gcc-14/changes.html) list `std::print`
and `std::println` among the additions. This supplies the compiler-specific
reason to retain the existing calls under the supported GCC 13 baseline.
All 337 issue-specific portability dispositions returned successful MCP
responses and were verified as FALSE_POSITIVE by exact issue key in `main`.
They are included in the completed main ledger and the 367-transition total.

Two S6494 findings instead recommended `std::format` or `std::format_to_n` for
`snprintf`, in `character_hex.cpp` and `close_smoke.cpp`. GCC implements this
formatting facility from 13.1. Both call sites remained OPEN after this review
as modernization suggestions; the review found no current behavior defect.
The [PR #220 follow-up](#2026-11-16-follow-up-after-pr-219) subsequently addresses
both call sites.
The character-conversion call formats a scalar derived from a validated
surrogate pair: its ` U+%06X` output occupies nine characters plus the terminator
within a 16-byte buffer. The adjacent UTF-16-unit formatting also fits that
buffer. The close-test trace uses six fixed transition labels and a pointer
converted to `const void*`; its longest output on the supported x64 platforms
occupies at most 45 characters plus the terminator within a 128-byte buffer.
Both calls pass their buffer size to `snprintf` and use fixed format strings.
S7040's delimited-escape recommendations also remain separate: GCC documents
P2290R3 support from GCC 13, and Microsoft documents it from MSVC Build Tools
14.50. The repository's recorded native Windows baseline uses toolset 14.51.
Sources: [GCC language support](https://gcc.gnu.org/projects/cxx-status.html),
[MSVC conformance](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance)
and [recorded native environment](issue20-README.md).

### Validation through `bc6ea68`

- Python helper tests: 53 run, 44 passed and nine platform/tool-dependent skips.
- Canonical clang-format 23.1.2: all 365 previously tracked C++ files passed;
  the new allocation fixture also passed its format check. `git diff --check`
  passed for the changes.
- Allocation reproduction: fault injection into the actual character buffer,
  word-list initialization and word-list insertion each reached the pre-fix
  terminate handler, recorded as exit 86. All three corresponding propagation
  checks passed after the fix. The storage regression passed in its ordinary
  build and with AddressSanitizer plus UndefinedBehaviorSanitizer, including
  failed-growth retry and preservation of every sorted candidate.
- The first [PR #219](https://github.com/1298345095/wxnotepad/pull/219) native X11
  run at `7b497c1` failed only `original_complete_word`, at the fixture's
  embedded-escape-style assertion. The retained `pr219-first-x11-failure.log`
  records the failure and the pinned core's offset of 47. Commit `bc6ea68`
  replaces both `+ 40` expectations with the core's `js_style` mapping.

The [hosted run for `bc6ea68`](https://github.com/1298345095/wxnotepad/actions/runs/43846682363)
recorded these results, including the allocation fixture and the affected
measurement, completion, zoom, chooser and drag scenarios:

| Platform | Native tests | Diagnostics | Additional validation |
| --- | --- | --- | --- |
| Windows | 213 passed, one skipped | 2/2 passed | 20 visual checks passed |
| GTK/X11 | 200 passed, two skipped | 2/2 passed | Native coverage collection succeeded |
| GTK/Wayland | 202 passed | 2/2 passed | 20 Wayland JSON checks passed; native coverage collection succeeded |

Fuzz and CodeQL jobs also succeeded at this revision. The corresponding
[PR #219 SonarCloud gate](https://sonarcloud.io/dashboard?id=1298345095_wxnotepad&pullRequest=219)
reported OK: new-code reliability, security and maintainability ratings were
A/A/A, coverage was 86.4%, duplicated lines were 0.0% and reviewed security
hotspots were 100%. The PR's OPEN security/reliability query returned zero.
The retained `pr219-bc6ea68-sonar.json` records the matching revision, analysis
task and query results.

### Allocator analysis follow-up at `5de95ab`

At `bc6ea68`, the Quality job's blocking diagnostic was a
`clang-analyzer-unix.Malloc` use-after-free report at wxWidgets `buffer.h:265`
while analyzing the GUI fixture and its replacement allocator in the same
translation unit. Review identified a reference-count modeling false positive:
the pinned [wxWidgets 3.3.3 buffer implementation](https://github.com/wxWidgets/wxWidgets/blob/v3.3.3/include/wx/buffer.h#L214-L267)
increments shared ownership, decrements the count on release and deletes the
data at zero. Its source also documents analyzer reports that assume data can
be deleted while it remains referenced.

The PR analysis at that revision contained seven HIGH findings in the new
fixture. The replacement-allocator S1231 and framework-owned frame S5025 were
verified as false positives as recorded above. Commit `5de95ab` addresses the
other five in code: three S5421 reports on namespace-level mutable state, one
S3624 report on the guard's copy/move contract and one S5008 report on the custom
`void*` allocation helper. Allocation hooks now live in
`tests/completion_allocation_fault.cpp`; a function-local `thread_local` state
supports the noncopyable, nonmovable guard, and allocation enters through the
standard `operator new` interface. All seven native scenarios retain their
existing actions and assertions.

The storage probes linked against the new hook module passed at `-O2` and with
AddressSanitizer plus UndefinedBehaviorSanitizer, each returning exit 0.
### Final validation at `5de95ab`

All four workflows for the final code revision succeeded:
[CI](https://github.com/1298345095/wxnotepad/actions/runs/43851081884),
[Quality](https://github.com/1298345095/wxnotepad/actions/runs/43851082062),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/43851081806) and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/43851082089).

The repeated native results were 213 passed and one existing IME skip on
Windows, 200 passed and two existing skips on X11, and 202 passed on Wayland.
Both diagnostic cases passed on each backend. The allocation-failure fixture
and `original_complete_word` passed on all three platforms. Both coverage
jobs and the existing 20-run Windows and Wayland repeat checks also passed.

Clang-tidy analyzed all 225 maintained translation units successfully. Both
`tests/completion_allocation.cpp` and
`tests/completion_allocation_fault.cpp` returned exit 0. The allocator boundary
change therefore passed the same analysis that reported the earlier wxWidgets
modeling false positive.

The SonarCloud scanner recorded SCM revision
`5de95ab252d31073907c226cdc1d0f43467201f2` and uploaded PR #219's analysis at
2026-11-16 10:56:18 UTC, with task `AaHkACiCYT84ZD0T3lrE`. The matching PR query
reported zero OPEN HIGH/BLOCKER findings and zero OPEN security/reliability
findings. The gate remained OK, with A/A/A ratings, 86.4% new-code coverage,
0.0% duplicated lines and 100% reviewed hotspots. These results refer to the
code revision; the concluding documentation commit records this completed
validation and the 373 verified dispositions.

## 2026-11-16 follow-up after PR #219

The continuation starts from `main` revision
`7ff2e5d1ced33065f40cfd0d1151635c18a9e0b1`, after PR #219 merged.
The current-project MCP baseline contains 3,988 OPEN issues, zero OPEN
HIGH/BLOCKER issues, zero OPEN security/reliability findings and zero security
hotspots awaiting review. Its quality gate is OK, with 87.9% new-code coverage
and 1.0% new-code duplicated lines. These measurements describe the starting
`main` analysis.

The follow-up branch is `codex/sonarcloud-followup-2026-11-16`, reviewed in
[PR #220](https://github.com/1298345095/wxnotepad/pull/220).

### Published changes

| Commit | Change and preserved behavior |
| --- | --- |
| [`67b2e399`](https://github.com/1298345095/wxnotepad/commit/67b2e39970cc48d44c048c336a58de97aa60d1f8) | Correct partial startup of the measurement test's worker group. `std::jthread` owns started workers, and missing participants leave the barrier before stack unwinding joins them. The normal four-worker, 64-iteration measurement checks and worker-exception type/message transport are retained. Also set the search-settings fixture's failure result explicitly when an exception follows its earlier success marker. |
| [`22c1728e`](https://github.com/1298345095/wxnotepad/commit/22c1728ee78edb5922c76ed5946794b8ef9d4838) | Address 22 reviewed baseline findings through typed formatting supported by GCC 13, removal of obsolete commented completion code, and direct Python dictionary construction. Character/Hex output, completion candidate strings and navigation byte dumps retain their bytes. Close-lifecycle tracing writes its pointer directly with the native `%p` format and retains its sequence/timing fields. |
| [`d641fc40`](https://github.com/1298345095/wxnotepad/commit/d641fc408137c7d9d0393b603c6068bc3b7c78f7) | Address five reviewed baseline findings around insertion-test flags and clipboard-peer cleanup. Validate a positive, single-bit flag in the range 1–256 before narrowing or indexing. Preserve the tag/word modes' separate argument contract. Report standard and unknown peer-shutdown exceptions while continuing the existing destructor cleanup. |
| [`835b6f3a`](https://github.com/1298345095/wxnotepad/commit/835b6f3a08f24e0723ba27f39c60362742f1c998) | Make the worker-failure regression portable after the first Windows run exposed its exception-pointer identity assertion. Check the concrete exception type and message while retaining all completion and thread-join checks. |

The formatting batch resolves the earlier continuation's two reviewed
`snprintf` follow-ups in `character_hex.cpp` and `close_smoke.cpp`.
Completion-allocation formatting remains outside each active fault-injection
guard, preserving which allocation the fixture tests.

### Issue-specific false-positive review

All 49 issue-specific false-positive transitions succeeded through SonarCloud
MCP. An exact-key query with the FALSE_POSITIVE filter returned every key;
the corresponding OPEN/CONFIRMED query returned zero. The current main OPEN
count changed from 3,988 to 3,939. The MCP search uses the legacy status string
RESOLVED in these filtered results; each mutation receipt explicitly records
falsepositive. The [follow-up disposition ledger](sonarqube-2026-11-16-followup-dispositions.json)
retains all keys, source explanations, receipts and the exact verification context.

| Rule | Count | Reviewed evidence |
| --- | --- | --- |
| S6032 | 12 | The copied document paths are before/after snapshots. Referencing the mutable document-path member would make later identity checks compare the member to itself and weaken the tests. |
| S6494 | 2 | The remaining reviewed `printf` recommendations require `std::print`, which the supported GCC 13 libstdc++ does not provide. These are distinct from the supported `std::format` changes above. |
| S3630 | 11 | Reviewed conversions preserve Scintilla's pointer-sized message ABI or Wayland's opaque-object C ABI. The intrinsic fixture's `long*` signature is exercised through adapters that copy exactly four bytes with `memcpy`, avoiding a `long` lvalue access on LP64. |
| S5276 | 8 | The actual values are bounded zoom levels, native integer coordinates or unsigned bytes. Their reviewed ranges remain exactly representable in the receiving type despite wider message-transport types. |
| S2738 | 9 | Worker and modal callbacks store `std::current_exception`, finish their required join/dialog cleanup and call `std::rethrow_exception` on the originating test path. The original exception is transported across a callback boundary. |
| S125 | 7 | Generated C++ scheme comments label standard-library types and header groups. They accompany active keyword data and preserve annotations from the pinned upstream scheme. |

The 137 S1181 findings were reviewed and remain OPEN: 133 are terminal wxApp
test-result boundaries and four preserve modal/timer failure information for
the calling test. Their catches serve a deliberate purpose. The rule's stated
exceptions do not explicitly list these framework callbacks, so this review
retains the implementation and records the design assessment without a bulk
false-positive or accepted-status transition. The search-settings result bug
fixed in `67b2e399` is separate from its broad-catch recommendation.

The 190 S112 generic-exception recommendations have no bulk false-positive
disposition. The 852 S7040 bounded-escape recommendations remain supported
syntax modernization suggestions; compiler portability does not justify
classifying them as false positives.

### PR analysis follow-up

The first PR #220 analysis reported ten OPEN findings. Each was reviewed
against the actual call sites and rule definition, marked FALSE_POSITIVE with
an individual explanation through MCP, and verified by exact issue key. The
PR query then returned zero OPEN/CONFIRMED findings and an OK gate, with A/A/A
ratings, 88.2% new-code coverage, 0.0% duplicated lines and 100% reviewed
hotspots. These are ten PR-context keys in addition to the 49 main-context
dispositions above. Both contexts and all 59 mutation receipts are retained in
the same disposition ledger.

| Rule | PR count | Reviewed evidence |
| --- | --- | --- |
| S6494 | 3 | The new failure/trace diagnostics use fixed `fprintf` formats on the supported GCC 13 build, whose standard library lacks `std::print`. |
| S7494 | 4 | Each `split(..., 1)` call produces the complete pair consumed directly by `dict`. SonarSource's own [rule fixture](https://github.com/SonarSource/sonar-python/blob/9ae0129e3f7b41be37b3b128e4b6447b467c278a/python-checks/src/test/resources/checks/rewriteCollectionConstructorAsComprehension.py#L44-L49) records this exact `dict(x.split(...) for x in ...)` pattern as `FP SONARPY-3154`. Reintroducing the copy-only comprehension would restore the original S7500 issue. |
| S1141 | 1 | The worker lambda's `try` executes on a separate thread. The outer `try` protects startup on the calling thread; deferred worker rethrows occur after that startup handler has ended. |
| S2738 | 1 | The startup handler removes unstarted barrier participants and executes `throw;`, matching the rule's explicit rethrow exception. |
| S3519 | 1 | All three internal `Typing` calls follow the positive, bounded, single-bit validation. In the character branch, the only inputs are 1–128 powers of two and the only indices are 0–7. The reported zero-input index 32 is unreachable; 256 takes the separate line-comment branch. |

The exception-transport review and regression check type and message.
[`current_exception` may copy an exception](https://eel.is/c++draft/propagation),
and [MSVC documents this behavior](https://learn.microsoft.com/en-us/cpp/cpp/transporting-exceptions-between-threads?view=msvc-170).
The ledger preserves the originally submitted S2738 comments and adds this
clarification to their use of the word "identity".

### Local validation

The [follow-up validation record](sonarqube-2026-11-16-followup-validation.json)
retains the baseline reproduction outcomes, completed probe exits, output and
source hashes, together with the matching hosted revision.

- Python helper suite: 53 tests run, 44 passed and nine existing
  platform/tool-dependent skips.
- Canonical clang-format 23.1.2 passed for all 370 tracked C++ files.
  `git diff --check` and Python syntax checks passed.
- The baseline worker-start reproduction reached the installed terminate
  handler after one or three workers had started (exit 86). Failure before the
  first worker propagated normally. The new shared-helper regression passed
  at `-O2 -Wall -Wextra -Werror` and with AddressSanitizer plus
  UndefinedBehaviorSanitizer. It covers startup failures after 0/1/3 workers,
  four workers completing 64 iterations, worker-exception type/message
  transport and a successful subsequent batch. LeakSanitizer's ptrace-environment error
  is retained in the first log; address/undefined checks passed with leak
  detection disabled.
- Formatting equivalence passed for all 18 frozen Character/Hex fixtures,
  1,112,064 Unicode scalars, 65,536 two-byte inputs, 256 allocation candidates,
  200 keyword suffixes and all 256 navigation byte values.
- Insertion-flag probes reproduced an oversized shift for `0`, `3`, `-1`
  and `2147483647`; `512` produced the out-of-range label index 9. The validated
  form rejects all five and maps every accepted flag 1–256 to indices 0–8.
  The native process fixture now requires ordinary failure exits and unchanged
  settings bytes for ten invalid-mode/flag combinations, while retaining all
  nine valid insertion settings and tag/word save/reopen checks.
- The original 32-bit intrinsic regression passed with AddressSanitizer and
  UndefinedBehaviorSanitizer, checking every bit position, adjacent guard
  storage and an individually allocated word.

### Hosted validation

The first [CI run at `d641fc40`](https://github.com/1298345095/wxnotepad/actions/runs/43865285948)
passed both Linux backends and both coverage jobs. Windows recorded 213 passes,
one existing IME skip and one failure: the new worker regression compared
exception-pointer identity, which MSVC's exception-copy behavior does not
preserve. Quality, Fuzz and CodeQL completed successfully at that revision.
Commit `835b6f3a` corrects the assertion as described above; its hosted rerun
completed successfully.

All four workflows passed for final code revision
`835b6f3a08f24e0723ba27f39c60362742f1c998`:
[CI](https://github.com/1298345095/wxnotepad/actions/runs/43868800359),
[Quality](https://github.com/1298345095/wxnotepad/actions/runs/43868800369),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/43868800500) and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/43868800464).
Windows recorded 214 native passes and one existing IME skip, and its
diagnostics passed. Ordinary and coverage Linux runs each recorded 201 passes and two existing skips on X11, and 203 passes on
Wayland. The new worker regression passed on every backend. Clang-tidy
successfully analyzed all 226 maintained translation units.

The scanner recorded that same SCM revision at 12:56:02 UTC and uploaded
task `AaHkbd7ojQBaE46LhdS_`. GitHub's SonarCloud Code Analysis check
`133312057643` completed successfully for that SHA at 12:56:10 UTC. The matching
MCP review returned zero OPEN/CONFIRMED findings, zero security hotspots
awaiting review and all ten PR false-positive dispositions still present.
The [PR gate](https://sonarcloud.io/dashboard?id=1298345095_wxnotepad&pullRequest=220)
is OK: A/A/A ratings, 88.2% new-code coverage, 0.0% duplicated lines and 100%
reviewed hotspots. The concluding documentation commit records these completed
checks against the code revision.

### Surface initialization follow-up

The PR #220 source review identified an allocation/native-resource failure boundary in
`WxSurface::Init`: both overloads are `noexcept`, while constructing their
device context or text-measurement state can throw. Its factories also returned
a surface without checking `Initialised`, and consumers dereferenced the result.
The [continuation below](#2026-11-16-surface-and-parallel-failure-follow-up)
implements coordinated initialization, factory, worker and host recovery, with
owned/borrowed context fault injection and document-history regression fixtures.

Source references: [wx surface adapter](../../src/editor/platform/wx_platform.cpp),
[GTK measurement](../../src/editor/platform/text_measurement_gtk.cpp),
[Windows measurement](../../src/editor/platform/text_measurement_msw.cpp),
[native core entry points](../../src/editor/editor_core.cpp),
[pinned surface contract](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/Platform.h)
and [pinned surface factories](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/src/Editor.cxx).

## 2026-11-16 surface and parallel failure follow-up

This continuation starts from merged `main` revision
`a308c6b181674358205df90669293cc4afcab3af` and reuses
`codex/sonarcloud-followup-2026-11-16`. The earlier branch tip
`8d4e6be10afc17db6b88690db3407f7a73d5f29c` supplies the unchanged application
baseline for the local failure reproductions and issue-by-issue source review.

### Resource failure fixes

- Build the owned drawing context and text-measurement state in local owners,
  then publish the complete `WxSurface`. Failed `Init` leaves a reusable,
  uninitialized surface under the pinned no-throw contract. A ready surface
  requires both drawing and measurement resources. GTK checks the Pango context,
  font map and Cairo font options; Windows checks the native DC and mapping
  queries before retaining their results. Pixmap creation checks its bitmap and
  context and publishes measurement state together with them.
- Check readiness at the wx drawing and default-measurement factories, so a
  failed initialization cannot reach an unconditional consumer dereference.
  DirectWrite's measurement-only surface retains its existing targetless
  initialization behavior.
- Reserve clip ownership storage before changing native clipping and allow the
  wx `SetClip` override to propagate allocation failure. Destroy nested clip
  scopes from the innermost outward before releasing their context. The latter
  fixes a separately reproduced native clipping-rectangle restoration error.
- Use the shared production `ParallelTasks::Run` in both layout and wrapping.
  Linux owns explicit `std::jthread` workers and retains their exceptions in
  separate slots; Windows captures exceptions inside the native thread-pool
  callback. Both implementations finish all started work
  before propagating failure and releasing the worker or its output buffers.
  A failed Windows work-item creation reports its original system error.
- Recover at the editor's message, native event, paint, IME and drag boundaries.
  Recovery records the failure status, resets transient painting/wrapping
  state, and invalidates partial style, font and pixmap caches for the next
  normal layout. A failed text drop reports failure to the native drag source.
  Feedback cleanup also contains its own measurement failure and clears stale
  drop-caret state.
- Complete mandatory document notifications after a visual resource failure.
  The pinned edit/Undo path contains individual visual updates while preserving
  contraction, selection, revision and history work. The wx notification bridge
  records mandatory document/selection changes before optional completion and
  queued UI work. This keeps subsequent edits and Undo/Redo available after an
  insertion has already committed its bytes.
- Preserve the original main row when style refresh or a retry reconstructs a
  rectangular selection. Undo restores that row after its final caret update,
  and a later paint retry preserves it when rebuilding the ranges.

The maintained core changes are in
[`patches/original-core.patch`](../../patches/original-core.patch); the source
archive remains pinned and SHA-256 checked. The same archive passed patch
validation and native compilation with the complete changes.

### Reviewed false positives

The [surface continuation disposition ledger](sonarqube-2026-11-16-surface-dispositions.json)
records 93 individually reviewed main-context findings, their exact source
contracts, submitted MCP comments and transition receipts.
All 93 transitions succeeded. An exact-key `FALSE_POSITIVE` query returned
every reviewed key; the corresponding `OPEN`/`CONFIRMED` query returned zero.
After the completed PR #220 main analysis and these dispositions, the current
main query contains 3,819 OPEN/CONFIRMED issues, zero HIGH/BLOCKER issues and
zero security/reliability findings.

| Rule | Count | Source evidence |
| --- | --- | --- |
| S1238 | 23 | The methods implement the pinned Scintilla `Surface`, `Window` or calltip signatures. Changing their by-value parameters to references would change the required declaration or override. |
| S6188 | 2 | `PolyLine` and `Polygon` implement the pinned pointer/count virtual interface; a span parameter would no longer override that interface. |
| S6022 | 60 | Character literals index the 256-character completion membership set through a word index and bit position. Their values describe language characters used in arithmetic. |
| S6022 | 8 | `GetRangePointer` transports the original `const char*` document text through the Scintilla message ABI; each reviewed consumer reads UTF-8 strings or individual characters. |

General modernization suggestions retain their existing review status. These
93 dispositions follow the actual declarations and data consumers for each
issue key.

### Regression coverage and local results

`native_surface_initialization_failures` uses the real editor and native drawing
contexts. It covers owned/borrowed initialization failure and retry, missing and
invalid contexts, drawing-factory rejection, allocation failures in nested
clipping, actual paint dispatch, and unchanged document bytes, selections,
Undo/Redo and save points. It also injects failure after insertion commits and
at successive visual allocations during Undo, checking continued editing and
history restoration. A three-line rectangular case checks every anchor, caret
and virtual-space offset, the original main row, and complete restoration after
paint retry, Redo and another Undo.

`native_parallel_surface_failures` executes the same task helper used by layout
and wrapping. It checks allocation and native-resource exceptions, pending
callbacks still writing their output, startup failure after 0/1/3 tasks, competing
worker/startup errors, native work-item creation failure on Windows, and a
successful subsequent batch. Existing parallel measurement and raster fixtures
retain their numerical comparisons.

Actual headless Cairo/Pango probes link the complete baseline or repaired wx
platform implementation. They use real native image contexts and the production
allocation-fault helper:

| Probe | Baseline outcome | Repaired outcome |
| --- | --- | --- |
| Owned initialization, failure allocating `wxClientDC` | Terminate handler, exit 86 | Uninitialized/reusable surface, exit 0 |
| Borrowed initialization, measurement object/data allocations | Terminate handler, exit 86 | Actual Cairo/Pango retry succeeds, exit 0 |
| Nested clip allocation sweep | Terminate handler, exit 86 | Three allocation points recover and retry, exit 0 |
| Release two nested clips | Wrong native clipping rectangle, exit 1 | Original native rectangle restored, exit 0 |
| Production parallel task regression with `get()` changed back to `wait()` | Lost worker failure detected, exit 1 | All tasks joined and exception preserved, exit 0 |

The [local validation record](sonarqube-2026-11-16-surface-validation.json)
retains source hashes, probe outputs, process results and build evidence.
The Python helper suite passes 44 tests with nine existing platform/tool skips.
Canonical clang-format 23.1.2, Python syntax and whitespace checks pass.
The production parallel helper also passes AddressSanitizer and
UndefinedBehaviorSanitizer; leak detection is disabled for the sandbox's ptrace
restriction.
The application and affected native fixtures compile and link against
wxWidgets 3.3.3 and the local GTK SDK. The local sandbox cannot create the display
sockets needed to execute GUI fixtures; the continuation pull request runs those
fixtures through the existing Windows, X11, Wayland and coverage jobs. Its checks
and description record the final hosted revision and analysis results.

### First hosted run and paint-hook correction

The first [CI run at `187ee05`](https://github.com/1298345095/wxnotepad/actions/runs/43881661997)
compiled both supported platforms. Each ordinary and coverage Linux backend
reported one failure in the new surface fixture's paint-injection assertion;
the preceding initialization, factory and clipping checks passed, as did the
production parallel-task fixture. X11 recorded 202 passes, two existing skips
and one failure; Wayland recorded 204 passes, one failure. Coverage analysis
stopped at its existing requirement that both backend test jobs pass.
Windows recorded the same single paint-injection failure, 215 passes and one
existing IME skip; its parallel-task regression and both diagnostics passed.
Ordinary Linux diagnostics passed on both backends, and its Wayland JSON check
passed all 20 repeated runs.

The cause was the test's handler arrangement. `NativePaint::Complete` pushed its
completion observer ahead of `PaintFailure`. wxWidgets' native event-chain
dispatch then restricted the inner handler to its own event table; its scoped
allocation failure expired before dispatch reached the editor's paint handler.
`PaintFailure` now waits for its own native paint completion and remains at the
head of the chain. The original assertions still require an actual injection,
`BadAlloc` status and an idle paint state.

A standalone probe linked against actual wxWidgets 3.3.3 reproduces that dispatch
behavior with a target event handler and the production allocation-fault helper:
the nested observers invoke the target after injection has expired, while a
single observer covers the target allocation. The corrected GUI fixture
compiles and links; its actual paint and subsequent document-history cases are
executed by the following hosted run. The validation record retains this first
failure, its diagnosis and the event-chain probe evidence.

[Quality](https://github.com/1298345095/wxnotepad/actions/runs/43881661891),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/43881662138) and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/43881662041) all passed
at `187ee05`. Clang-tidy analyzed all 228 maintained translation units.

### Backend-dependent allocation sweeps

The [second native CI run at `6ae1477`](https://github.com/1298345095/wxnotepad/actions/runs/43886806315)
passed all Windows regressions: 216 passes and one existing IME skip, plus both
diagnostic tests. Its complete surface fixture passed, including paint,
post-commit notification recovery and ordinary/rectangular Undo.

On both ordinary and coverage Linux backends, paint and post-commit recovery
also passed. Their single remaining failure was the stream Undo sweep's final
coverage assertion. The saved GCC 13.3 counters establish the cause directly:
the loop body and `++injectedCount` each executed 32 times, while
`completed = true` and its `break` executed zero times. All 32 injected runs
passed their text, selection, revision, save-point, Redo and subsequent-edit
checks. The fixed `skip < 32` bound ended the sweep before a successful
uninjected run; the minimum failure-coverage requirement was already met.

Both Undo sweeps now advance through matching allocations until the first
uninjected run passes every invariant. They retain the minimum two/three
injection requirements, print the actual injected count and completion index,
and use the existing 60-second CTest timeout. This also removes the rectangular
sweep's unverified 48-allocation cutoff. The corrected fixture compiles and
links locally. The validation record retains the original CI counters and
matching-gcov commands, and the next hosted run exercises the complete Linux
sweeps.

[Quality](https://github.com/1298345095/wxnotepad/actions/runs/43886805903),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/43886806327) and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/43886806308) also
passed at `6ae1477`.

### Complete native validation and PR #221 analysis

The third code revision, `2d6cdba1fcf5e3fc5804053d6130b3a56742cd09`, passed
[CI](https://github.com/1298345095/wxnotepad/actions/runs/43891433929),
[Quality](https://github.com/1298345095/wxnotepad/actions/runs/43891433966),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/43891434007) and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/43891433953).
The actual native checkout was merge revision
`1956f400a7b0f432c95436bd3cfd53761de639b1`.

| Platform | Ordinary native tests | Diagnostics | Coverage native tests |
| --- | --- | --- | --- |
| Windows | 216 passed, one existing IME skip | 2/2 passed | Collected on Linux |
| X11 | 203 passed, two existing skips | 2/2 passed | 203 passed, two skips |
| Wayland | 205 passed | 2/2 passed | 205 passed |

Every surface fixture completed all nine stages. Windows recorded five stream
Undo injections and seven rectangular Undo injections; both ordinary and
coverage Linux backends recorded 130 and 182 respectively. Each sweep then
completed its first uninjected run with all original invariants intact.
Parallel-task recovery, the 20-run Wayland JSON check, runtime packaging and
SDK checks also passed. Clang-tidy analyzed all 228 maintained translation units.

The scanner uploaded PR #221 at 15:19:45 UTC with SCM revision `2d6cdba1` and
task `AaHk8V2RSw0uFDSXC1ay`. Its independent SonarCloud gate reported ERROR:
new-code coverage was 79.0%, below the existing 80% threshold. Ratings were
A/A/A, duplicated lines were 0.0%, and reviewed hotspots were 100%.

### Template coverage export correction

The retained GCC 13 counters show executed exception paths in
`parallel_tasks.h` and `editor_core.h`. The generated generic XML nevertheless
contained multiple `lineToCover` elements for each template source line.
Later zero-hit instances caused SonarCloud to report already executed source
lines as uncovered.

Both export commands now use gcovr 8.8's
[`--merge-lines`](https://www.gcovr.com/en/stable/manpage.html#cmdoption-merge-lines).
It combines source-line hits and corresponding branch identities while keeping
the raw instances in the intermediate JSON. Re-exporting the same third-run
JSON reproduced the original XML byte for byte before the change. The corrected
export reduced 11,681 elements to 11,259 unique source lines, eliminating all
74 duplicated source lines. Complementary branches in `parallel_tasks.h`
became 2/2; the partially covered `editor_core.h` handler remained 1/4, and
its unexecuted runtime-error handler remained uncovered.

The coverage regressions exercise actual template instantiations with
complementary branches, repeated same-side branches, and explicit uncalled
instantiations. They check unique XML source lines and retained raw JSON
instances alongside the existing shared-header and exception-path checks.
Using actual GCC 14/gcov 14 in a scratch copy, the old export failed both
report tests and the corrected export passed all six tests. The repository's
native fixture remains pinned to GCC 13 and runs in CI. Source scope, ordinary
uncovered conditions and the existing 80% gate are retained.

### Findings reviewed from the first PR #221 analysis

The [PR disposition ledger](sonarqube-2026-11-16-surface-pr-dispositions.json)
records all 72 findings from this analysis, including 36 HIGH findings,
zero BLOCKER/security findings and two reliability findings. Thirty-three
issue-specific false-positive transitions succeeded through MCP; exact-key
queries returned all 33 as FALSE_POSITIVE and zero as OPEN/CONFIRMED.

| Rule | PR false positives | Reviewed contract |
| --- | --- | --- |
| S5019 / S3608 | 12 | Six inner lambdas execute immediately through the native recovery wrapper, matching the rules' synchronous-use exception. Their outer stored callbacks have explicit captures. |
| S859 | 3 | The pinned `const noexcept` notification override runs on the privately owned, non-const `Core` object; error recovery modifies that original object. |
| S5008 | 7 | `DocWatcher` requires opaque user-data parameters. The fixture registers `nullptr` and leaves each parameter unused. |
| S1238 | 2 | Geometry and document-modification value parameters implement pinned virtual declarations. |
| S5025 | 1 | The native top-level fixture window follows wxWidgets' deferred destruction and application cleanup ownership. |
| S6494 | 5 | The supported GCC 13 standard library lacks the recommended `std::print`. |
| S2738 | 3 | Exception pointers retain failures across worker cleanup and rethrow them on the originating path. |

The same batch addresses 25 findings in code. Linux worker startup now uses
explicit `std::jthread` ownership instead of `std::async`. Per-worker exception
slots outlive every thread, all started threads join before error propagation,
and startup errors retain priority. The partial-start regression holds started
workers behind a latch until the observing thread releases them, checking the
0/1/3-worker failure cases and later retry. It passes at `-O2` with strict
warnings and under AddressSanitizer/UndefinedBehaviorSanitizer.

Fixture cleanup also makes asynchronous captures explicit, prohibits copying
and moving the completion guard, documents the five intentional empty watcher
callbacks, keeps paint observations private, and applies the reviewed const,
enum-conversion and diagnostic-expression improvements. The surface fixture
and affected core translation units compile and link with these changes.

Fourteen recommendations retain their review status: seven concern generic
exception types, three concern deliberate recovery/test-result catch boundaries,
three suggest `using enum`, and one counts wxWidgets' inherited class depth.
The continuation [PR #221](https://github.com/1298345095/wxnotepad/pull/221)
records the final hosted revision, resulting issue queries and gate after this
combined code and report-export batch.

### GCC 13 coverage verification and final fixture cleanup

Revision `7e57caea1acce20a425612087ec81ee538b916d6` passed
[CI](https://github.com/1298345095/wxnotepad/actions/runs/43898310949),
[Quality](https://github.com/1298345095/wxnotepad/actions/runs/43898310953),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/43898311086) and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/43898310979).
The native checkout was merge revision
`215e0e758d7eeccea65bad8cc1705847dc45a3fb`.
Windows again passed 216 ordinary tests with one existing IME skip; X11 passed
203 with two existing skips; Wayland passed all 205. All platform diagnostics,
the nine-stage surface fixtures, worker recovery and the 20-run Wayland JSON
check passed. Both coverage backends passed all six coverage-report regressions
using the actual GCC 13 compiler. Each backend and the merged report contained
11,263 source-line entries across 98 files, with zero duplicate source lines.

The matching Sonar task `AaHlEIeaLosZbpaI6AUv` reported 83.0% new-code coverage:
132 of 153 new lines and 131 of 164 new conditions were covered. The existing
80% coverage requirement passed. Its sole failing gate condition was reliability:
the partial-start fixture retained an unreachable throw after
`std::rethrow_exception`. The expected `std::system_error` path returned from its
handler, while other exception types propagated to the test runner.

The fixture now ends after its typed handler; the unreachable throw and its
redundant return are removed. The captured-exception, original-error-code and
joined-worker-count assertions remain. The drawing-factory check also receives
its editor through the existing const observer interface. Both native fixtures
compile and link, and the unchanged parallel regression passes at `-O2` with
strict warnings. The removed throw also carried an existing S112 suggestion.

Exact-key queries confirmed all 25 prior code findings FIXED. Of the first 33
PR false-positive keys, 28 retained their disposition and five DocWatcher keys
closed and reappeared under new keys after explanatory comments changed the
method bodies. Those five required-ABI findings, a worker-lambda exception-boundary
finding and a test-thread exception-transport finding were individually marked
FALSE_POSITIVE through MCP. All seven new transitions were verified by exact-key
queries. The PR disposition ledger retains their comments, receipts and mapping
to the five replaced keys.

The final two fixture changes and these completed validation records are submitted
together. [PR #221](https://github.com/1298345095/wxnotepad/pull/221) records the
resulting analyzed revision, final gate and remaining recommendations.

## 2026-11-16 type safety and exception follow-up

This batch continues from `b015dda89776e3dda74f093e9656d36792143a7e` on the
same branch and PR #221. At that revision CI, Quality, Fuzz and CodeQL passed;
the PR quality gate was OK with 83.0% new-code coverage. Fresh MCP queries
returned 3,819 OPEN/CONFIRMED findings on main and 13 in PR #221. Main's
analysis predates the unmerged surface continuation, so those contexts are
recorded separately in the [issue-by-issue review](sonarqube-2026-11-16-type-safety-review.json).

### Small code changes

The changes address 60 distinct report keys: 46 from main and 14 from PR #221.
Counts distinguish report keys across analysis contexts, including keys referring
to the same code. The first hosted analysis verified six PR keys FIXED. The
ledger records the other changes as implemented on the follow-up branch; main's
issue statuses are updated by analysis after the branch is merged.

| Rule | Keys | Change |
| --- | ---: | --- |
| S112 | 12 | Give native drawing/measurement failures a dedicated exception type and use dedicated test assertion failures. |
| S5350 | 8 | Hold completed dialog controls through read-only observer pointers. |
| S995 | 5 | Qualify read-only frame, text-control and event parameters. |
| S5945 | 4 | Store fixed encoding, file-watching, line-ending and accelerator tables in `std::array`. |
| S6004 | 10 | Limit scalar and local result lifetimes to the condition and branch using them. |
| S6009 | 2 | Read synchronous fixture strings through `std::string_view`. |
| S7035 | 9 | Extract enum underlying values explicitly while retaining message-width conversions. |
| S5276 | 2 | Recover the actual unsigned-byte character/style result type at the message boundary. |
| S6177 | 8 | Use function-local enum imports in seven existing test functions. |

`NativeDrawingError` identifies rendering and measurement failures that have no
platform error code. Its `std::runtime_error` base preserves the existing surface,
message, native-event and notification recovery boundaries. The original
diagnostic strings, `std::system_error` codes, allocation failures and length
checks are retained. The equivalent Windows GDI/DirectWrite throw sites use the
same category. The parallel-task regression now transports this exact exception
type and message through real workers, checks every started worker has finished,
and then runs a successful batch. The prior allocation and system-error cases
remain in the same fixture.

The dialog table contents and order are unchanged. Native wx constructors still
receive their pointer/count arguments through `data()` and `size()`. The const
observers and event callbacks were checked against the pinned wxWidgets 3.3.3
headers for both supported platforms. The narrowed completion variables are
scalar values; their evaluation order and short-circuit behavior are unchanged.
Both string views remain inside synchronous calls whose caller-owned strings
outlive the call.

The rescan identified two implicit integer conversions in completion. The
pinned core returns `unsigned char` from both `Document::UCharAt` and
`Document::StyleIndexAt`, transported by the general `intptr_t` message ABI.
The new `CharacterAt` and `StyleIndexAt` accessors restore that result type
before the existing character operations. `Sci_Position` positions retain
their width, and promotion into the existing integer character parameters
preserves the entire byte range. The enum imports are confined to individual
test functions and preserve their operations, assertions and scenarios.

### Ten verified false positives

Each transition was submitted through the SonarCloud MCP with its own source
explanation. All ten transitions succeeded. Exact-key queries in their
respective analysis contexts returned nine main keys and one PR key under the
FALSE_POSITIVE filter; matching OPEN/CONFIRMED queries returned zero. The ledger
preserves every comment and successful receipt.

| Rule | Keys | Reviewed evidence |
| --- | ---: | --- |
| S995 | 1 | `SetAccessibleName` calls the non-const `wxWindow::SetAccessible` on Windows and writes GTK accessibility state. The Linux compilation omits the Windows mutation. |
| S995 | 3 | Zoom key, wheel and menu helpers deliver input through native handles/event handlers, modifying the target editor or frame-owned UI. Const-qualified handle accessors hide these indirect mutations. |
| S6188 | 2 | `CompareNoCase` shares a function-pointer slot with `strncmp`; its count is a maximum comparison bound and comparison also stops at NUL. The callback preserves that pointer/count contract. |
| S1000 | 1 | The unnamed namespace belongs to the completion implementation; the allocation fixture intentionally includes that implementation to exercise its private storage in the same translation unit. |
| Python S7494 | 2 | Both geometry parsers use the `dict(split(...) for ...)` pattern explicitly recorded as FP SONARPY-3154 in SonarSource's own rule fixture. |
| S5025 | 1 | The line-ending `wxChoice` is owned by its parent dialog. Both native wx backends register the child and destroy it through the parent's `DestroyChildren` path. |

The Python findings follow the same evidence as the earlier PR #220 review:
[SonarSource's pinned regression fixture](https://github.com/SonarSource/sonar-python/blob/9ae0129e3f7b41be37b3b128e4b6447b467c278a/python-checks/src/test/resources/checks/rewriteCollectionConstructorAsComprehension.py).
The existing split-on-first-equals parsing retains its duplicate-key and
malformed-line behavior.

### Validation and remaining recommendations

The first published revision, `ae986b9d59380bba8bb9e2a0a2443776bf1c9a37`, passed
[CI](https://github.com/1298345095/wxnotepad/actions/runs/43917497292),
[Quality](https://github.com/1298345095/wxnotepad/actions/runs/43917497150),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/43917497348) and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/43917497156).
Native jobs used merge revision `7eb5b2d4dd903d5e272f5ff5274ec61db1beeddb`.
Windows passed 216 tests with one existing IME skip; X11 passed 203 with two
existing skips; Wayland passed all 205. Each ordinary platform passed both
diagnostic tests. Both coverage backends passed, including completion allocation,
zoom and surface/parallel failure fixtures. Clang-tidy analyzed 228 maintained
translation units. Native SDK checks, packaging, the repeated Windows brace
case and the 20-run Wayland JSON regression also passed.

Sonar task `AaHleCwYOC3kQQYL28Di` analyzed that exact source revision and reported
an OK gate, 82.3% new-code coverage, A reliability/security/maintainability,
0.0% duplication and 100% hotspot review. Exact-key queries returned all six
original PR S112 findings with the FIXED filter. The rescan also surfaced the
two byte-result conversions, additional enum-scope suggestions and the
parent-owned choice allocation. Their completed code changes and verified
false-positive disposition are recorded above.

The follow-up passes canonical clang-format 23.1.2 over 374 tracked files and
`git diff --check`. GCC 14.2 compiles the production parallel-task fixture with
C++23, `-O2`, `-Wall -Wextra -Werror` and pthread support; the documented
`ci.py probe` executes it successfully with exit 0. Current source hashes and
the probe result are in the review ledger. The commit-specific checks and latest
continuation section on [PR #221](https://github.com/1298345095/wxnotepad/pull/221)
record the hosted validation after this rescan follow-up.

Four PR recommendations remain for a separate design review: three concern
the established recovery/test-result catch boundaries, and one counts
wxWidgets' inherited application depth. S1181's documented exceptions do not
cover these ordinary returning callbacks and methods. Their recovery purpose
alone is insufficient for a false-positive disposition, and removing the
runtime-error handlers would lose worker/native-resource recovery. The allocation
fault helper's S6004 finding was already fixed at the starting revision and is
excluded from this batch's counts.

## 2026-11-16 small-fix continuation

This batch starts from merged main revision
`c3d98ca4d270f2367b85ec9a96e0a14e582830ad` on
`codex/sonarcloud-small-fixes-2026-11-16`. The current main analysis at
19:01:05 UTC had an OK quality gate and 3,767 OPEN/CONFIRMED findings.
An explicit security/reliability filter returned zero OPEN/CONFIRMED findings;
the selected work addresses maintainability findings. Exact-key queries also
confirmed that all 46 main-context code-fix keys from the preceding type-safety
batch are now FIXED after PR #221 merged.

The [issue-by-issue review](sonarqube-2026-11-16-small-fixes-review.json) records
23 code-fix keys, eight verified false-positive dispositions, source evidence,
transition receipts and retained recommendations. Code-fix entries describe the
follow-up branch; main receives those fixes through a later merge and analysis.

### Small code changes

| Rule | Main keys | Change |
| --- | ---: | --- |
| S872 | 12 | Express boolean counting, bit tests, buffer offsets and persisted caret flags with logical operations, conditional increments or explicit integer alternatives. |
| S1238 | 7 | Read alignment spacing, rectangle values, expected ranges, colours and constructor inputs through const references. |
| S5276 | 2 | Recover the completion character's unsigned-byte type and make the bounded polygon point-count conversion explicit. |
| S5416 | 1 | Express the existing constant keyword pointer-array alias with `using`. |
| S2209 | 1 | Call `FindWindowByName` through its class while retaining the explicit dialog search root. |

This addresses every S872 and S1238 key in the current baseline. The caret
encoding retains its original decimal positions and INI values. Mixed-line-ending
classification still tests for at least two nonzero counters. Unicode statistics,
LaTeX byte lengths, search anchor counts and fixture selection endpoints keep
their original values and evaluation order. The two native test drivers still
copy paths into their own `wxString` members before starting their timers.

`ExtendDocumentWord` now calls the existing `CharacterAt` accessor. The pinned
core's `Editor::WndProc(GetCharAt)` returns `Document::UCharAt`, an unsigned byte,
through the general pointer-sized message result. The accessor restores that
known byte domain before the existing character comparisons.

The polygon count was checked against every caller in the pinned core revision
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`. Call tips and indicators use three
points; marker polygons use 3, 4, 5, 8 or 12 points. The explicit `int` conversion
therefore preserves the entire reachable count domain. Dynamic polyline point
counts have a different source and retain their separate finding.

### Eight verified false positives

Each disposition was applied through the SonarCloud MCP with an individual
source-to-effect explanation. All eight transitions succeeded. Exact-key
queries returned eight findings with the FALSE_POSITIVE filter and zero with
OPEN/CONFIRMED. The API's legacy status field says RESOLVED; the disposition
filter and transition receipts establish the specific false-positive resolution.

| Rule | Keys | Reviewed contract |
| --- | ---: | --- |
| S5495 | 2 | Sort and tab settings copy the reported closures into retained native event callbacks. wxWidgets `Bind` stores a copy of the functor for later dispatch. |
| S995 | 5 | Completion key/wheel/double-click, selection mouse input and automatic typing mutate their target widgets through native handles and events. Const-qualified handle getters obscure these writable aliases. |
| S5350 | 1 | The tab-settings helper casts a top-level window to the same dialog object, edits its controls and calls `EndModal`. The framework cast preserves the writable alias. |

These contracts were checked against the pinned wxWidgets 3.3.3 event storage,
native handle accessors and `wxDynamicCast` implementation. The ledger links
those exact framework sources and the application/test paths that consume them.

### Local validation

Canonical clang-format 23.1.2 passed for all 374 tracked C/C++ files, and
`git diff --check` passed. Independent complete-source review checked parameter
lifetimes, single evaluations, persisted values and the character result type.
The documented core preparation verified the pinned archive and patch set.
This workspace has no wxWidgets SDK; native execution uses the hosted checks.

### First hosted validation

The first published revision, `e3cebb53b35032db55defcb53ea5ada227877011`, passed
[CI](https://github.com/1298345095/wxnotepad/actions/runs/43945699409),
[Quality](https://github.com/1298345095/wxnotepad/actions/runs/43945699336),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/43945699453) and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/43945699715).
Native jobs checked out merge revision
`094a8bc7c0a090c2d6035e7467829204ed0f74da`.

| Platform | Native tests | Skipped cases | Diagnostics | Repeated regression |
| --- | --- | --- | --- | --- |
| Windows | 216 passed, 1 skipped | `windows_inline_ime_input` | 2/2 passed | Visual brace: 20/20 passed |
| X11 | 203 passed, 2 skipped | `original_new_window_maximized`, `native_peer_geometry_ready` | 2/2 passed | — |
| Wayland | 205/205 passed | None | 2/2 passed | JSON: 20/20 passed |

Both coverage backends completed successfully. The
[first Sonar task](https://sonarcloud.io/api/ce/task?id=AaHmCEfquUWaKV8HNVdA)
recorded the exact source revision above and an **ERROR** quality gate:
new-code coverage was 76.36363636363636% (displayed as 76.4%), below the 80%
threshold. All 13 new lines were covered; 13 of 42 new conditions were
uncovered, giving `(13 + 29) / (13 + 42)` combined coverage. The missing
conditions were localized as follows at that revision:

| Source | Conditions | Covered | Uncovered |
| --- | ---: | ---: | ---: |
| `src/document/encoding/encoding_detection.cpp:255` | 8 | 2 | 6 |
| `src/document/encoding/encoding_detection.cpp:261` | 8 | 2 | 6 |
| `src/editor/completion_selection.cpp:31` | 3 | 2 | 1 |

PR-scoped queries returned zero OPEN/CONFIRMED findings and zero security
hotspots. Every other gate condition passed: A reliability, security and
maintainability; 0.0% new duplication; 100% hotspot review. This first hosted
result remains recorded independently of the follow-up validation.

### Focused coverage follow-up

The selected follow-up adds regressions to two existing fixtures:

- `automatic_encoding_decisions` distinguishes valid UTF-16 code units containing
  CR/LF-valued bytes from byte line endings that cross code-unit boundaries or
  end the stream. The valid control checks automatic Open, exact decoded text,
  encoding/BOM identity and byte-for-byte Save. Three CR/LF samples compare all
  five `DetectEncoding` identity fields with Unicode statistics enabled and
  skipped; the skipped control must choose a non-UTF-16 fallback without a BOM.
  This retains the platform's native/legacy fallback, including Windows UTF-8
  ACP mode.
- `SupplementaryLatex` in the existing automatic-completion fixture types `i`
  after `\bb` through native input, then presses Tab with Tab fill-up disabled
  and LaTeX input enabled. The `\bbi` command produces U+1D55A (`𝕚`) through
  `CompleteLatex`'s two-UTF-16-unit path. It checks the exact four UTF-8 bytes,
  immediate anchor/caret position 4, popup closure, Undo to `\bbi` at 4/4 and
  Redo to the glyph with the grouped replacement's saved selection at 0/0.
  The existing `original_automatic_completion` and Windows
  `native_directwrite_automatic_completion` targets execute this case.

At the first revision's `encoding_detection.cpp:261`, the true outcome of
`previousHigh == 10 && previousLow == 13` cannot reach the final check: its
`0x0A0D` code unit was already rejected earlier in the loop. The follow-up
targets reachable cross-unit and terminal paths while preserving the
production predicates and the 80% coverage threshold.

A GCC 14.2 probe of the extracted production encoding helpers passed all four
byte streams with exit 0 through `ci.py probe`. Its gcov results reached 8/8
cross-unit branch outcomes and 7/8 terminal outcomes, consistent with the
unreachable outcome identified above.

### First follow-up failure and corrected selection expectation

Follow-up revision `116551daf2cee9fcb06c9a76113dae9373eb393d` checked out merge
revision `e25fb234d6412a96d1ce9a961f180c21016803a3`. Its
[Wayland coverage job](https://github.com/1298345095/wxnotepad/actions/runs/43951652825/job/133595404264)
passed `automatic_encoding_decisions` in 0.44 seconds. At 20:47:50 UTC,
`original_automatic_completion` failed with `Selection anchor/caret: 0/0;
expected 4/4` and `LaTeX conversion Redo restores the UTF-8 caret`. The exact
output, popup closure, immediate 4/4 selection, Undo text and 4/4 selection,
and Redo text assertions had all passed before this final assertion.

The same revision's
[Windows job](https://github.com/1298345095/wxnotepad/actions/runs/43951652825/job/133595404333)
reported the same 0/0-versus-4/4 failure in both `original_automatic_completion`
and `native_directwrite_automatic_completion`: 214 passed, two failed and
`windows_inline_ime_input` skipped. `automatic_encoding_decisions` passed in
1.67 seconds, diagnostics passed 2/2, and the visual-brace repeat passed 20/20.

The pinned source explains the saved selection: `ReplaceTarget` owns an
`UndoGroup`. Its closure reaches `Document::EndUndoAction`, then
`Editor::NotifyGroupCompleted` and `RememberCurrentSelectionForRedoOntoStack`,
which save the replacement's empty selection at 0/0 before `CompleteLatex`
performs its final `SetSel` to 4/4. The
[pinned original LaTeX handler](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/win32/ScintillaWin.cxx)
also calls `ReplaceTarget` before `SetEmptySelection`, and its default selection
options enable selection history. This source contract and the hosted Linux
and Windows results establish the corrected Redo assertion at 0/0. The immediate and Undo
assertions remain 4/4; production behaviour is preserved.

The corrected revision `cf95ea8d7d2f2f3cb73c22322cee8b02d086a74b` passed
[CI](https://github.com/1298345095/wxnotepad/actions/runs/43955000805),
[Quality](https://github.com/1298345095/wxnotepad/actions/runs/43955000673),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/43955000767) and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/43955000718).
The PR quality gate is OK: new-code coverage is 98.2%, with all 13 changed
coverable lines and 41 of 42 conditions covered. The PR issue query returns
zero OPEN/CONFIRMED findings. This completes the earlier pending validation;
the issue-by-issue ledger retains both failed attempts and the successful result.

### Retained work

The four recommendations retained by PR #221 remain mapped to their current
main keys in the ledger: two production recovery boundaries, the surface test's
result boundary and its wxWidgets inheritance depth. Their earlier design-review
rationale remains applicable. Broader S1181, S110 and S112 recommendations keep
their existing status.

The platform's remaining coordinate conversions require explicit rounding,
clipping and out-of-range behaviour. Dynamic squiggle polylines also require a
separate point-count bound. The following continuation supplies the matching
`Platform::Assert` declaration and begins the S7040 literal-data pass with
byte-preservation checks. The earlier definition-only compilation failure
remains recorded in the small-fix ledger.

## 2026-11-16 declaration and literal continuation

This batch continues on PR #222 from
`cf95ea8d7d2f2f3cb73c22322cee8b02d086a74b`. The starting head passed all four
workflows and had an OK SonarCloud gate. Main had 3,759 OPEN/CONFIRMED findings;
the explicit security/reliability filter returned zero. The
[issue-by-issue review](sonarqube-2026-11-16-declarations-literals-review.json)
records 227 main-context code-fix keys and four verified false-positive
dispositions. Main receives code fixes through merge and its subsequent analysis.

### Small declaration and expression changes

| Rule | Main keys | Change and checked contract |
| --- | ---: | --- |
| S3646 | 13 | Separate existing named local types from their objects. Preserve each initializer, enclosing block, construction point and destruction order. |
| S1121 | 6 | Split chained assignments in their existing right-to-left write order, retaining plain boolean fields and the engaged indentation override. |
| S6228 | 1 | Use `!std::has_single_bit(mask)` after the control byte has set a bit, so the checked mask is nonzero. Retain the existing adjacency short circuit. |
| S6045 | 1 | Use `std::less<>` for the fixture's local string map, preserving its existing string-key ordering and lookups. |
| S5271 | 1 | Add `[[noreturn]]` to both the wx `Assert` definition and its first prepared-core declaration. |

The declaration split is limited to the original local type and immediately
following object declaration. Reversing the 13 splits restores all 11 files
byte for byte to the starting revision. Event registration, callback data
addresses, fixture cleanup and GTK transfer lifetimes retain their original
order and scopes.

The wx `Platform::Assert` implementation always reports its diagnostic and calls
`std::abort`. Its prepared `Debugging.h` declaration receives the attribute only
under `__WX__`: the pinned original Windows implementation has returning
Ignore/Retry paths and a non-trace implementation. The original reference target
keeps that contract. The prepared header and exact production definition compile
together with GCC 14.2, C++23 and strict warnings; a caller ending in `Assert`
also compiles without a missing-return diagnostic. The non-wx returning control
definition compiles with the same header.

### Literal data with verified byte preservation

S7040 applies to 205 current-main keys across four maintained tables:

| File | Main keys | Complete string groups compared |
| --- | ---: | ---: |
| `tests/escape_text_fixtures.h` | 136 | 204 |
| `tests/character_hex_fixtures.h` | 36 | 54 |
| `tests/custom_case_fixtures.h` | 6 | 108 |
| `src/editor/special_insert.h` | 27 | 54 |

The actual numerical escapes use bounded C++23 forms such as `\x{e2}` and
`\u{2028}`. Fixture text that describes escape syntax retains its literal
backslashes. The compiler compares complete original and final formatted
string arrays: their types, lengths and every byte, including embedded and
terminating NULs. All 420 groups match. The separate token comparison retains
all nonliteral tokens, explicit lengths, table ordering, command/state values
and original comments. Existing original-observation provenance is preserved.

The supported GCC 13 baseline implements
[P2290R3](https://gcc.gnu.org/gcc-13/changes.html), as does
[Clang 15 and later](https://clang.llvm.org/cxx_status.html), including the
clang-tidy 18 frontend. Microsoft's
[conformance table](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170)
lists support from Build Tools 14.50. The starting head's successful
[Windows job](https://github.com/1298345095/wxnotepad/actions/runs/43955000805/job/133607101050)
used MSVC 19.51.36260.0 / Build Tools 14.51.36231 with the repository's C++23 mode.
GNU gettext 0.22.5 was built from its official source archive. Running the
repository's localization extractor with that exact `xgettext` exits zero and
produces a byte-identical 46,187-byte `wxnotepad.pot`. Its only diagnostic is the
existing `--sort-output` deprecation notice. The ledger records the command,
output hashes and matching lexer source review for the untranslated byte fields.

### Four verified false positives

The two `EncodingPicker::Notify` findings and two `ReloadPicker::Notify`
findings in `tests/chinese_encoding.cpp` suggest pointer-to-const for `window`
and `dialog`. Complete source and pinned wxWidgets 3.3.3 review establish that
these aliases refer to the same mutable dialog. The drivers select its controls
and dispatch OK/Cancel through `GetEventHandler()->ProcessEvent`. The framework
then reaches `wxDialogBase::OnButton`, `AcceptAndClose` / `EndDialog` and
`EndModal`, modifying the dialog's modal state and return code. The const getter
and `wxDynamicCast` conceal this writable alias from the analyzer.

All four issue-specific FALSE_POSITIVE transitions succeeded through MCP with
their source-to-effect explanations. Exact-key queries return four with the
FALSE_POSITIVE filter and zero with OPEN/CONFIRMED. Main now has 3,755
OPEN/CONFIRMED findings; the unmerged code-fix keys remain in that context.
Comments, exact keys, framework source hashes and receipts are in the ledger.

### Validation and remaining work

Canonical clang-format 23.1.2 passes all 374 tracked C/C++ files and
`git diff --check` passes. The documented core preparation validates the pinned
archive and complete patch set. The strict compiler checks above and the
420-group data comparison pass; the latter executes through `ci.py probe` with
exit zero. This batch adds no persistent tests. Existing hosted checks validate
the combined branch; their exact-head results are recorded on
[PR #222](https://github.com/1298345095/wxnotepad/pull/222).

At that checkpoint, 647 S7040 keys remained for file-specific data review.
Coordinate rounding/clipping, dynamic polyline bounds, the occurrence-marking
duration conversion and the previously documented exception/inheritance
recommendations retain their separate review status and current keys.

### Completed hosted validation for the declaration/literal batch

Revision `ac7fab65c98ee200a9ff307c838abe066351e7c1` passed all 12 GitHub
checks, including the Windows and Linux native suites, both coverage backends,
clang-tidy, formatting, fuzzing, CodeQL and SonarCloud. The completed workflows
are [CI](https://github.com/1298345095/wxnotepad/actions/runs/43963778445),
[Quality](https://github.com/1298345095/wxnotepad/actions/runs/43963778643),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/43963778358) and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/43963778369).
Fresh PR-scoped MCP queries before the following continuation confirm an OK
SonarCloud gate, zero OPEN/CONFIRMED findings and 96.96969696969697% new-code
coverage: 19 of 20 lines and 45 of 46 conditions. New duplication is 0.0%.

The earlier ledger now includes these completed checks alongside its historical
publication-time pending status. This records the result for `ac7fab65` before
the next batch changes the branch head.

## 2026-11-16 byte-literal continuation

This batch continues PR #222 from
`ac7fab65c98ee200a9ff307c838abe066351e7c1`. Main contained 3,755
OPEN/CONFIRMED findings; its explicit security/reliability query returned zero.
The [issue-by-issue record](sonarqube-2026-11-16-byte-literals-review.json)
adds **647 code-fix keys and 10 verified false-positive dispositions**.
Including the earlier #222 batches, the branch addresses **897 distinct main
code-fix keys and 22 reviewed false positives**. Main receives the code fixes
through merge and its subsequent analysis.

### Literal and declaration changes

| Rule | Main keys | Change |
| --- | ---: | --- |
| S7040 | 18 | Bound actual numerical escapes in eight application files: BOMs, legacy encoding bytes, replacement characters, search ESC and collation weights. |
| S7040 | 628 | Bound actual numerical escapes in 82 existing fixture and reference-data files. |
| S5350 | 1 | Make `Untitled::menus` in `tests/reload_encoding.cpp` a pointer to const; its consumers only find menu items and read their enabled state. |

The literal changes replace 2,225 numerical escapes in 90 files. Source review
follows the actual decoding, encoding, save, search and collation consumers.
The independent fixture records retain their original captured values and
provenance. Raw strings, text describing backslash syntax, conventional
one-digit octal controls, explicit byte lengths and `s` literal suffixes retain
their original form. The application predicates, parser grammar, commands,
selection endpoints, history expectations and native event paths are unchanged.

The menu declaration was checked against wxWidgets 3.3.3's common and GTK
`FindMenuItem`, `FindItem` and `IsEnabled` implementations and the Windows
inherited interface. Those calls already use const methods and perform queries.

### Compiled data equivalence and formatting

GCC 14.2, C++23, `-O2 -Wall -Wextra -Werror -pedantic-errors` compares all
**14,816 complete literal groups**, including the **651 changed groups**,
extracted from the starting and final formatted sources. Compile-time assertions
check their types, complete array sizes and every element, including embedded
and terminating NULs. The comparison also passes with `-fshort-wchar`, checking
the same data with 16-bit wide characters. Both executables run successfully
through the documented `ci.py probe` entry point with exit zero.

Whole-file lexical comparison preserves every nonliteral code token and comment
through the literal commits. It also verifies raw strings and literal suffixes.
A separate lexer independently checks the first 41-file fixture group, and the
second 41-file group receives complete source and diff review. Large frozen
tables receive full data/token comparison and review of their complete consumers
and changed records; the ledger distinguishes these checks from manual source
reading.

The initial formatting pass exposed clang-format 23.1.2 splitting a long
delimited escape in `chinese_encoding.cpp`. The local check rejected that output.
Splitting long strings between complete escape units before canonical formatting
preserves valid syntax and yields matching complete literal arrays. The final
374-file canonical formatting check and `git diff --check` both pass.

This continuation adds no persistent tests. It uses two literal commits
(`8a593c7` for application code and `6d157ed` for fixtures), followed by the
menu declaration and records, published together in one branch update.
The existing hosted workflows validate that combined head; their resulting
checks are recorded on [PR #222](https://github.com/1298345095/wxnotepad/pull/222).

### Ten verified writable-alias false positives

The reviewed S5350 suggestions cross `wxDynamicCast`, native handles or
`GetEventHandler()`. Complete application paths and wxWidgets 3.3.3 definitions
establish mutation of the same reported object:

| File / driver | Keys | Same-object modification |
| --- | ---: | --- |
| `western_encoding.cpp`: `Picker`, `WarningDriver` | 3 | OK/Cancel/No dispatch changes the dialog's return code and modal state. |
| `current_encoding.cpp`: `EncodingPicker`, `ConversionPrompt` | 2 | Direct cancellation or dispatched button handlers call `EndModal` on that dialog. |
| `gbk_native.cpp`: `NativeDriver` | 1 | Native loss-confirmation input completes the reported dialog. |
| `save_copy.cpp`: `CopyChooser` | 1 | Native OK/Cancel input completes the Save Copy loss dialog. |
| `reload_encoding.cpp`: `ReloadDialogs` | 3 | Two window/dialog aliases reach modal completion; the child alias becomes the same list box and changes its selection. |

The `reload_encoding.cpp` if-initializer finding is specifically the
**Inconsistent Line Endings** dialog's Cancel path. Its issue is kept distinct
from the later Reload With Encoding dialog variable.

Each MCP transition has its own explanation, source path and framework trace.
Exact-key queries return all 10 as FALSE_POSITIVE and none as OPEN/CONFIRMED.
Main's remaining OPEN/CONFIRMED count is **3,745**, including this PR's unmerged
code fixes. Submitted comments, transition receipts, verification counts and
framework blob/source hashes are retained in the issue record.

### Remaining work

One of the previously remaining 647 S7040 recommendations is retained:
`AaG1r0uxyu4V9oip4YId` in `src/editor/platform/generated/sort_nls_data.h`.
It covers the frozen **497,834-byte** compressed NLS stream emitted by
`tools/capture_sort_nls.py`. Applying bounded syntax to every byte would add
995,668 source bytes and change 15,558 data lines for this one style finding.
The coordinated generator/payload representation review remains separate;
the issue stays open.

The original capture exporters also retain their existing JSON-style UCN output.
A later reference recapture can reproduce that spelling. Current builds consume
the checked-in, value-identical reference tables; their captured observations
and provenance are preserved.

Coordinate rounding/clipping, dynamic polyline bounds, occurrence-marking time
precision and the documented exception/inheritance recommendations keep their
existing individual review status. Their exact keys and source-contract evidence
remain in the preceding review records.

## 2026-11-16 array follow-up

The byte-literal head `85f296e8e1e05da2c57362807e75ecb583aeb0df` completed all
12 GitHub checks and all four workflows successfully. Its SonarCloud gate was
OK: new-code coverage **95.2%** (37/38 executable lines and 82/87 conditions),
duplicated-lines density **0.9671%** (shown as 1.0% in the gate), and all three
new-code quality ratings A. The gate's coverage and duplication limits remain
80% and 3% respectively.

That completed analysis reported two OPEN S5945 findings in the PR: `invalid`
in `utf16_smoke.cpp` and `fixtures` in `visual_brace.cpp`. Their existing array
declarations entered the new-code range after literal formatting. Complete
source and rule review confirms that both are fixed object arrays to which the
std::array recommendation applies. This follow-up also handles the two other
already-open S5945 findings in those same files.

| File / object | Main issue key | Change |
| --- | --- | --- |
| `utf16_smoke.cpp` / `invalid` | `AaGQhgSWusAIaC0DBL7m` | `const std::array<std::string, 5>`; same malformed-byte cases and index-zero access. |
| `utf16_smoke.cpp` / `payloads` | `AaGQhgSWusAIaC0DBL7s` | `const std::array<std::string, 3>`; same empty, 22-byte and 2-byte payloads. |
| `visual_brace.cpp` / `fixtures` | `AaG7NURwkxw1cHak340H` | `const std::array<EncodedFixture, 3>`; same UTF-8 / UTF-16LE / GBK records. |
| `visual_brace.cpp` / `font` | `AaG7NURwkxw1cHak340F` | Zero-initialized `std::array<char, 256>` with `.data()` at all four pointer consumers. |

The PR keys `AaHmz451xHJDvo969oYO` and `AaHmz5NnxHJDvo969oYP` correspond
to the first and third rows. The other two rows were verified in the main-context
MCP query. All four are code fixes.

The object arrays are only traversed by const-reference range-for loops, with
one existing `invalid[0]` access. The `EncodedFixture` consumer receives one
element at a time. The font buffer retains its capacity and local lifetime;
the fixed upstream core's `StyleGetFont` path writes through `char*`, including
the terminating NUL. Its two native sends, string construction and comparison
all receive `.data()`.

These four additional, non-overlapping main keys bring PR #222 to **901** code
fixes and **22** verified false positives. The continuation from `ac7fab6`
accounts for **651** code fixes and **10** false positives. Main's review-time
OPEN/CONFIRMED count remains 3,745 while the code changes await merge and main
analysis.

The canonical clang-format 23.1.2 check passes all 374 tracked C/C++ files, and
`git diff --check` passes. This follow-up adds no persistent tests. The four
changes and completed preceding-run evidence are grouped into one additional
commit; its existing native/coverage/quality/fuzz/CodeQL workflows verify that
head. Follow-up results are recorded on [PR #222](https://github.com/1298345095/wxnotepad/pull/222).

The [array follow-up record](sonarqube-2026-11-16-array-followup-review.json)
contains exact main/PR issue mappings, before/after source hashes, complete
consumer review and the completed `85f296e` check/measure snapshot. The generated
NLS table and other separately documented boundary/precision reviews retain
their existing status.

## 2026-11-17 small continuation after PR #222

PR #222 merged at 2026-11-17 05:08:15 UTC as
`e45dfe4ec6fd32f7622cd5647df2b7e7d3b5c825`. This batch starts from that
merged revision on `codex/sonarcloud-small-followup-2026-11-17`.
Fresh main-context MCP queries after the disposition below return **2,841**
OPEN/CONFIRMED findings and **zero** security/reliability findings. The earlier
3,745 count above is the historical, pre-merge snapshot.

The [issue-by-issue record](sonarqube-2026-11-17-small-followup-review.json)
records **13 code-fix keys and one verified false positive**, with source blob
hashes and validation scope. At publication, main reported the 13 code-fix keys
OPEN. The [subsequent enum review](#enum-cleanup-after-pr-223) confirms all 13
FIXED after merge and main analysis.

### Reviewed changes

| Rule | Main keys | Change and preserved contract |
| --- | ---: | --- |
| S5945 | 2 | Store the private GB18030 and Big5 compatibility tables in fixed `std::array` objects. All 22 records, ordering and range-for consumers retain their values. |
| S1659 | 2 | Separate the Big5 range fields and the six fixture-row strings into individual declarations, retaining types, order, initialization and scope. |
| S6004 | 2 | Limit the byte `lead` and `optional<wchar_t>` compatibility result to their existing conditions/branches. Evaluation order and fallbacks are retained. |
| S3562 | 1 | Make the CP950 switch's existing fallthrough explicit with `default: break`, continuing into the same single-byte/private-use mapping code. |
| S7034 | 4 | Use `contains` for three file-variable character-membership checks and one fixture substring check. Preserve delimiter bytes, guards and the NUL-terminated needle contract. |
| S7035 | 2 | Extract enum underlying values before the existing `uintptr_t`/`unsigned` casts in the language helper, retaining message widths and fold masks. |

The application changes are in `1b3282b4`; the shared language-helper changes
are in `70624df3`. The helper retains all native actions, fixture bytes,
font/highlighting checks, fold assertions and Undo checks. Code and observation
commits are prepared together before one branch publication. This batch adds
no persistent test targets and leaves the workflows and analysis scope unchanged.

### Verified cross-platform false positive

S6004 key `AaHEzXZMhpq0YxBZ9SBn` recommends moving `EncodeChinese`'s `text`
into its empty-check initializer. Complete source shows that the `__WXMSW__`
branch subsequently reads `text->size()` and supplies `text->data()` to both
`WideCharToMultiByte` calls. The common declaration keeps that conversion
storage alive and in scope. Linux-only analysis omits these Windows consumers.

The declaration is retained. Its issue-specific SonarCloud MCP transition
succeeded with the platform-use explanation. Exact-key queries return one
FALSE_POSITIVE match and zero OPEN/CONFIRMED matches; the legacy status field
is RESOLVED. The ledger records the receipt and source evidence.

### Local validation and hosted scope

All three complete baseline files match their Git blob hashes. Reversing the
explicit edits restores those baseline bytes. Clang 17 compiles the extracted
compatibility helpers and complete file-variable parser in before/after
namespaces with C++23 and `-O2 -Wall -Wextra -Werror -pedantic-errors`.
The comparison exits zero and reports:

- 22 identical table records, with matching structure sizes, alignments and
  Big5 field offsets.
- 2,228,228 encoding comparisons, 3,624,382 decoding comparisons and
  3,624,380 sequence-width comparisons, covering both encoding choices,
  all one/two-byte inputs and every GB18030-shaped four-byte sequence.
- 554,344 file-variable parser comparisons covering option flags, quotes,
  embedded NULs, BOMs, every byte delimiter and the 512-byte windows.
- 873 standard-library membership comparisons and compile-time enum-cast
  equivalence controls.

Clangd 17's formatter reports zero edits for the three changed files with the
repository's `.clang-format`. Canonical clang-format 23.1.2 and the complete
native builds run in the existing hosted workflows; this local workspace has
no wxWidgets SDK and the connected Windows runner is offline. The continuation
PR records the exact published head and its resulting checks. At publication,
those hosted results are pending; earlier revisions' successful checks remain
attributed to their own revisions above.

### Retained recommendations

The generated NLS payload's S7040 issue keeps its coordinated generator/data
review. Coordinate rounding and clipping, dynamic polyline count bounds,
occurrence-marking time precision and the documented exception/inheritance
recommendations retain their existing statuses. These behavior-sensitive items
remain separate from this batch's declaration and membership changes.

## Enum cleanup after PR #223

This batch starts from merged PR #223 at
`395ad59521dff2e7a5fb1c660188d73ea2269726`. Exact-key SonarCloud MCP queries
confirm all **13** preceding code-fix keys as FIXED (legacy status CLOSED).
The merged revision's [CI](https://github.com/1298345095/wxnotepad/actions/runs/44037068945),
[Quality](https://github.com/1298345095/wxnotepad/actions/runs/44037069139),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/44037069117), and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/44037069080)
workflows all completed successfully.

The fresh main baseline contains **2,828** OPEN/CONFIRMED findings, including
**463 S6177** and **381 S7035**, with zero security/reliability findings.
The [issue-by-issue record](sonarqube-enum-cleanup-review.json) maps every
priority-rule key to a disposition: **843 code fixes and one verified false
positive**. Four additional occurrence-marking fixes and three shared-helper
fixes bring this batch to **850 code-fix keys** across **155 source and test
files**. After the false-positive
transition, main has **2,827** OPEN/CONFIRMED findings; this count includes the
code fixes awaiting merge and main analysis.

### Reviewed code changes

| Rule | Main keys | Change and preserved contract |
| --- | ---: | --- |
| S6177 | 462 | Import each enum in its reported existing local scope and use its unqualified constants there. Full enum member sets and platform branches were checked for name collisions. |
| S7035 | 381 | Extract enum values with `std::to_underlying`. Retain an explicit destination cast where its width or signedness differs from the underlying type. |
| S5945 | 2 | Store occurrence-marking lookup tables in `constexpr std::array<uint8_t, 304>` and `constexpr std::array<uint32_t, 89>`, preserving every value and indexed consumer. |
| S6009 | 2 | Pass read-only input to `CaseSensitiveSelection` and the shared language helper's `Unhex` as `std::string_view`. Callers retain their owning strings; `Unhex` still materializes the same small `std::stoi` arguments. |
| S5276 | 1 | Make the occurrence-marking rate denominator's existing integer-to-double conversion explicit. Search offsets, calculation order, rounding and the 100 ms budget retain their behavior. |
| S112 | 2 | Throw the existing `TestEditor::Failure` for original style/fold oracle mismatches, matching `Require`. Existing diagnostics, messages and `std::exception` result boundaries are retained. |

The enum changes preserve native message parameter types, pointer-sized integer
destinations, bit masks, array indices and fixture assertions. Direct `<utility>`
includes provide `std::to_underlying` on both native platforms. Two additional
Windows-only `Technology` conversions in `editor_frame_commands.cpp` follow
the same reviewed conversion contract. Source reading and repository searches
establish declarations, callers and consumers; CodeGraph was not used.

Application changes cover 39 files and fixtures cover 116 files. Existing
string/character literals, fixture data and command order pass complete-file
token/transform checks. An independent review covers all application diffs and
the second fixture group, including enum-name sets, integer destinations and
the four occurrence-marking consumers.

The three shared-helper changes address the remaining `Unhex`/generic-exception
items recorded in PR #223. Complete helper and caller review covers
`syntax_config_languages.cpp`, `syntax_native_languages.cpp` and
`syntax_markdown.cpp`; those three translation units were recompiled after the
header changes. The `Unhex` parser keeps its substring contents, numeric parsing
and exception behavior, and each fixture's existing catch boundary handles the
dedicated failure type through its `std::runtime_error` base.

### Verified Win32 name collision

S6177 key `AaG7NUVWkxw1cHak342E` reports `SelfDrags` in
`tests/stream_drag.cpp`. `DragApp::InitMainDrags` reaches that function, whose
Windows branch calls unqualified `GetFocus()` to verify the activated editor's
native input focus. The pinned `Scintilla::Message` enum also declares
`GetFocus = 2381`. Importing that enum into the function hides the Win32 function
and makes the call ill-formed. S6177 explicitly excludes name collisions and
shadowing; the Linux analysis omits the Windows call.

The qualified enum uses are retained in this scope. The SonarCloud MCP
false-positive transition succeeded with this exact source/platform explanation.
Exact-key queries return one FALSE_POSITIVE match and zero OPEN/CONFIRMED
matches (legacy status RESOLVED). A C++23 compiler control uses the actual
prepared `ScintillaMessages.h` and a global function declaration: it compiles
without the import and rejects the imported version because
`Scintilla::Message::GetFocus` cannot be called as a function.

### Local validation and combined publication

GCC 14.2 performs C++23 syntax compilation of all **151 changed translation
units**: 38 application and 113 test files. Their dependency files also reach
all **four changed headers**. The check uses official wxWidgets 3.3.3 public
headers, real GTK 3 development headers and the pinned prepared Scintilla core.
Its wx header configuration combines upstream GTK defaults with host platform
definitions produced by wx configure. Application and test `char8_t` settings
match their CMake targets. The ledger records each successful file and its
checked source hash; hosted native builds supply linking and GUI execution.

Compiled controls additionally check 133 enum import combinations and 16 enum
conversion types at underlying minimum/zero/one/maximum values. The occurrence
comparison checks all **393** table elements and **54** floating-rate cases,
including counts around `2^53` and the pointer-sized integer maximum. Results
match by complete floating-point bit representation. Both control executables
exit zero through the documented `ci.py probe` entry point.

Canonical clang-format **23.1.2** passes all **374** tracked C/C++ files, and
`git diff --check` passes. Application code, fixture code and this review record
are grouped into three commits before one branch publication. The existing
Windows, Linux X11/Wayland, coverage, clang-tidy, fuzz, CodeQL and SonarCloud
workflows validate that combined head. Hosted results are pending at publication
and are recorded on the continuation PR. This batch adds no persistent test
targets.

### Retained recommendations

The previously documented occurrence-marking precision finding is addressed by
the explicit rate conversion above. The remaining tracked recommendations keep
their OPEN status and individual source-contract review:

| Area | Rule / keys | Remaining decision |
| --- | --- | --- |
| Drawing/window coordinates in `wx_platform.cpp` | S5276, 20 keys listed in the ledger | Choose consistent range, rounding and clipping behavior and verify native rendering. |
| `PolyLine` point count | S5276, `AaG1r0ztyu4V9oip4YJD` | Establish bounds for dynamic `Indicator` callers. The separately reviewed fixed `Polygon` limit does not bound these inputs. |
| Generated NLS stream | S7040, `AaG1r0uxyu4V9oip4YId` | Coordinate the generator and frozen 497,834-byte compressed payload representation. Expanding every escape adds 995,668 source bytes and changes 15,558 data lines. |
| Recovery and test-result boundaries | S1181, three keys listed in the ledger | Retain the reviewed exception contract while considering the rule's design recommendation. |
| Framework inheritance | S110, `AaHlwkx59oTYNiRoCpVy` | Review the existing framework-derived type hierarchy as a design change. |

## PR #224 analysis follow-up

The first combined head of [PR #224](https://github.com/1298345095/wxnotepad/pull/224),
`226c9bec0d17d6416157d02de816a44bc35ba747`, passed all **12 checks** and all
four hosted workflows on their first attempt:
[CI](https://github.com/1298345095/wxnotepad/actions/runs/44044479796),
[Quality](https://github.com/1298345095/wxnotepad/actions/runs/44044479641),
[Fuzz](https://github.com/1298345095/wxnotepad/actions/runs/44044479595), and
[CodeQL](https://github.com/1298345095/wxnotepad/actions/runs/44044479591).
These results include Windows native tests, Linux X11/Wayland tests and coverage,
clang-tidy, canonical formatting, fuzz sanitizers and both CodeQL languages.
The SonarCloud PR quality gate passed with **90.8% new-code coverage** and
**1.2% new-code duplication**. The PR analysis reports zero S6177, S7035,
security or reliability findings.

That analysis surfaced **50 other findings** on changed lines. Exact-key,
source and rule review maps all 50 to existing main findings: **38** are small
code changes completed in this follow-up, **11** repeat existing verified false
positives, and **one** remains an open control-flow recommendation. The
[follow-up evidence record](sonarqube-enum-followup-review.json) contains the
complete mapping, submitted MCP comments, source hashes and validation results.
The two pairs of nested-conditional issues that share a source line are mapped
as exact two-key sets; the available issue ranges do not distinguish their
individual columns.

### Additional code changes

These **38 additional main keys** are disjoint from the first batch's 850,
bringing this PR to **888 main code-fix keys**. They affect 11 application files
and 11 fixture files already included in the first batch.

| Rule | Additional main keys | Change and preserved contract |
| --- | ---: | --- |
| S3358 | 15 | Expand nested conditional expressions into ordered branches while retaining default values, lazy native calls and evaluation order. |
| S5827 | 11 | Use `auto` for initialized locals with explicit type expressions. Retain destination casts and the mutable `int` required by the `int&` search interface. |
| S6004 | 6 | Move values used by a single condition into its `if` initializer, retaining diagnostic availability and reference lifetimes. |
| S5945 | 4 | Use `std::array` for the seven folding markers, two fixture message tables of five and ten entries, and four JavaScript dialects; preserve every entry and its order. |
| S7034 | 1 | Express the existing literal substring-presence check with `contains`. |
| S6005 | 1 | Bind the fixture's structured pair by `const` reference to its live range element. |

Complete-source and caller review verifies native message widths, enum defaults,
string ownership, fixture bytes, marker order and command order. Separate reviews
of both the application and fixture diffs found no blockers. The shared folding
header's complete set of **13 direct test consumers** was also checked.

### Reapplied PR false-positive dispositions

SonarCloud assigned new PR issue keys to 11 previously reviewed main false
positives. Their main dispositions were confirmed with exact-key MCP queries.
Each PR key was independently checked against the current source and marked
FALSE_POSITIVE through MCP with its own explanation. The final query returns
**11 FALSE_POSITIVE matches and zero OPEN/CONFIRMED matches** for these keys.

| Rule | PR keys | Verified source contract |
| --- | ---: | --- |
| S6022 | 2 | The character pointers represent document text and flow into character/string consumers. |
| S3630 | 2 | The pinned core returns the original `const char*` through pointer-sized `sptr_t`; dispatch preserves the value, the receiver restores the same pointer type, and consumers finish before document edits. |
| S5276 | 3 | Native character messages return `unsigned char` or `int`; restoring the transported value to `int` preserves its source range. Document positions and widths retain their separate native types. |
| S6494 | 3 | The supported Linux CI compiler uses GCC/libstdc++ 13. The required `<print>` support starts in GCC 14.1; existing fixed-format calls and arguments were checked. |
| S5813 | 1 | The fixture's immutable token points to one of three terminated string literals, each present in its paired input, before the checked `strlen` call. |

The first batch's separate Win32 `GetFocus` collision remains the one newly
classified main false positive. The 11 transitions above apply to the PR's
repeated findings. The evidence record retains both sets of keys and the
successful MCP responses.

### Follow-up validation and remaining work

C++23 syntax compilation passes **31 translation units**: all 11 changed
application units and 20 fixture units, including every direct consumer of
`native_fold_helpers.h`. The compiler configuration matches the first batch,
including each target's `char8_t` setting. Canonical clang-format **23.1.2**
passes all **374** tracked C/C++ files, and `git diff --check` passes.

Application code, fixture code and this evidence update form three further
commits, published together through one update of the existing PR branch.
The full PR therefore contains six commits in two CI batches. The follow-up
head's hosted results are pending at publication and are recorded in the PR
description when available.

S924 remains OPEN for PR key `AaHopBwfcf_I1OScw0J3`, corresponding to main key
`AaGwmVUYEeGJK6USKRqT` in `StripSelectedBlanks`. Its three loop exits enforce the
selection endpoint, terminal replacement and terminal leading-line advance.
Changing them requires a separate control-flow review that preserves
`FindTextFull` call order, undo grouping and selection repair. The previously
recorded coordinate, dynamic point-count, NLS generator, exception-boundary and
inheritance recommendations remain listed in the first batch's evidence record.
