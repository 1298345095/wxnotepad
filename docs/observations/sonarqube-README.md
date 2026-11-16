# SonarQube analysis

Current project: `1298345095_wxnotepad`, default branch `main`.

Commit `66cbd148` changed the configured project key from
`1298345095_wxnotepad2` to `1298345095_wxnotepad`. The November 4–10
baselines and dispositions below describe the earlier project and the application
revisions named in each section. Current-project findings are reviewed separately
against their current issue keys; the continuation record appears at the end.

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

| Rule | Count | Evidence supporting the PR disposition |
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

Two S6494 findings instead recommend `std::format` or `std::format_to_n` for
`snprintf`, in `character_hex.cpp` and `close_smoke.cpp`. GCC implements this
formatting facility from 13.1. Both call sites were reviewed and remain OPEN as
modernization suggestions; the review found no current behavior defect.
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
