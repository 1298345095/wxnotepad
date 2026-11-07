# SonarQube analysis

Project: `1298345095_wxnotepad2`, default branch `main`.

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
analysis. Python targets the CI interpreter version, 3.12. The three generated language
scheme tables are excluded from copy/paste metrics because their contents and
ordering come from the pinned upstream schemes. Other code analysis remains
enabled for those tables. Quality-gate thresholds and rule profiles are unchanged.

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
