# Responsibility and ownership refactor

Baseline: `4fa3240a8a572c42b4e6e1e821d20ac1cea23ec2`.

The review followed command entry points, state ownership and file/format
dependencies across `app`, `document`, `editor`, `settings` and `syntax`.
The implemented changes concentrate behavior that previously required knowledge
of several unrelated concerns in one class or translation unit.

## Findings and changes

| Area | Finding | Implemented responsibility |
| --- | --- | --- |
| Frame commands | One 2,670-line translation unit mixed file, editing, presentation, settings, shortcut and native-dialog dependencies. | The composition/file-command unit is 593 lines. Editing, view, settings and keyboard bindings have their own implementation files. Zoom dialog construction joins the other native dialogs. |
| Wrapping state | Startup, New/Open, settings capture, toggles and the dialog each knew how to distinguish the window preference from a file's `truncate-lines` override. | `WordWrapState` owns preference capture, override application, wrap symbols and accepted choices. The dialog edits `WordWrapOptions` instead of copying the complete application settings. |
| Document saves | `DocumentSession` mixed document identity and encoding decisions with temporary files, replacement, OS permissions, timestamps and backup naming/fallback. | `DocumentFiles::SaveTransaction` owns prepared writes and cleanup. `DocumentBackups` owns backup publication. The session retains save/load ordering and commits document state after success. Its implementation falls from 1,063 to 777 lines. |
| Automatic typing | The Core notification handler implemented typing policy while related completion settings, callbacks and state lived on its host. | `TypingAssistance` owns automatic indentation, paired insertion, tag closure and completion notification state. Core forwards notifications after recording changes and retains native adaptation and exception handling. |
| Style persistence | `SettingsStore` combined configuration lifecycle with style-specific parsing, defaults, delta serialization and import/export. | `StyleProfileIO` owns those format operations; `SettingsStore` retains file identity, theme transitions and saved snapshots. Its implementation falls from 775 to 644 lines. |
| Syntax selection | The static language catalog also implemented filename, favorite, extension, shebang, markup and INI detection precedence. | `syntax_detection.*` owns automatic detection and consumes the catalog through `SyntaxCatalog()`/`SyntaxFor()`. The catalog falls from 1,473 to 1,153 lines and keeps language metadata. |

The line counts describe where maintainers now navigate. The state and operation
ownership changes are the reason for the new modules. The frame remains the
window's composition point, consistent with the existing architecture.

## Preserved transition rules

### File transactions

`Prepare` reserves a sibling file and checks destination access. `Commit` writes
all bytes, flushes/closes the sibling, replaces the destination and applies the
selected timestamp policy. Destroying an uncommitted transaction removes its
sibling.

The session still chooses the preparation point. Save Copy creates a new empty
destination before cleanup and encoding confirmation. Original-time saves
capture timestamps on their first preparation. Save cleanup checks write access
before changing editor text. The destination is supplied at both preparation and
commit, preserving the existing lookup after modal encoding confirmation.

Backup publication falls back to the platform backup directory only after a
failure to create the sibling backup. A partial write is cleaned up and returned
as a failure at that location.

### Typing lifetime and notification order

Typing state is constructed before Core and outlives Core's shutdown. The
notification order remains: record document changes, process typing/completion,
then notify ordinary UI state. Completion acceptance consumes its notification.
The original ordering of newline indentation, tag/member access, paired insertion
and typed-word completion remains in one module, covered by the existing
native input and allocation-failure regressions.

### Window wrapping preference

Before a document transition, the live mode becomes the remembered preference
only when the current file has no override. Successful Open applies the new
file's optional override. New restores the remembered preference. Saving settings
while a file override is active writes the remembered preference. Explicit
toggles and accepted dialog choices update it. The editor owns its remembered
nonzero mode and rendering geometry.

## CI regression diagnosis

### Windows path identity

The new cancelled-Open assertion compared the stored document path with a
temporary filename containing a Windows TEMP 8.3 alias. Open expands that alias
to its long path. The fixture now captures `DocumentPath()` after successful Open
and checks that cancellation and failure preserve that loaded identity, alongside
the unchanged wrapping override. The [Windows run](https://github.com/1298345095/wxnotepad/actions/runs/45701119594/job/139511462464)
passed 355 tests with four existing environment skips.

### Linux wrapping geometry and invalidation

The original CI log reported the Unicode representation restore-row assertion
without native geometry or Core state. Follow-up diagnostic runs reproduced the
same assertion and established two failure mechanisms; the original log does not
identify which mechanism occurred in that run.

First, `SetSize(360, 300)` could update wx's cached size while GTK still held the
previous allocation. The visible menu imposed a minimum width of about 374 pixels,
so the initial wrapping baseline and restored representation used different
viewports. The fixture now owns a `FromDIP(360, 300)` client area with the menu
hidden, waits for actual frame-client and editor-outer geometry through
`NativePaint::Complete`, and checks wrapping before pixel reads can drain the
pending resize. The original wrapping, document-state and pixel assertions remain.

With that geometry fixed, the scale2 case still failed in all four diagnostic
sessions. [Run 45722459969](https://github.com/1298345095/wxnotepad/actions/runs/45722459969)
recorded these Core values at 100% zoom:

| Representation | Rows | Stored wrap width | Paint client width |
| --- | --- | --- | --- |
| Visible | 15 | 314 | 354 |
| Hidden | 8 | 314 | 360 |
| Restored | 14 | 320 | 354 |

The recorded horizontal margin was 40, and all three states had no pending
wrap. GTK scrollbar changes could alter the logical client width without a size
event, leaving wrapping calculated for a different viewport. `Core::Draw` now
compares `wrapWidth` with the text width derived from the client size already
read for that paint and calls `NeedWrapping()` when they differ. This uses the
existing paint path and paint count.

The [focused native validation](https://github.com/1298345095/wxnotepad/actions/runs/45723034114)
passed all 308 runs: 20 observed X11 controls, 100 observed scale2 reproductions,
40 cleaned X11 fixtures, 100 cleaned X11 scale2 fixtures, 24 cleaned Wayland
fixtures and 24 cleaned Wayland scale2 fixtures. The observed scale2 sequence is
now 15 / 8 / 15 rows; every captured Core wrap width matches that paint's text
viewport. The cleaned fixture retains the original exact restoration assertion
and uses no diagnostic observation mode.

### Native fixture responsiveness

The automatic-insertion and tag-closure fixtures ran 444 and 424 cases in one
GUI callback, delaying accessibility requests across otherwise successful cases.
Each fixture owns a one-shot timer and next-case index: one turn completes one
independent case. Teardown stops the timer; all text, selection, Undo/Redo and
saved-byte inputs and assertions remain intact.

The [insertion comparison](https://github.com/1298345095/wxnotepad/actions/runs/45738358923)
and [tag-closure comparison](https://github.com/1298345095/wxnotepad/actions/runs/45741569011)
each passed eight sessions with every functional case intact. External AT-SPI
`Peer.Ping` calls were bound to the actual application PID: each original pair
produced six 10-second timeouts during the case loop. The modified groups returned
133 replies for insertion and 228 for tag closure across X11 and Wayland, with no
mid-loop timeout; the clean registered CTests also passed on both backends.

### Windows subprocess cleanup

A caret fixture completed its assertions but temporary executable removal raised
WinError 32; the failing run did not identify the holder of that sharing conflict.
`tests/temporary_directory.py` owns a five-second monotonic cleanup retry deadline
for Windows sharing violations, with pauses of at most 50 ms; other errors propagate.
Successful cleanup preserves body exceptions; persistent conflicts reach the deadline.

The [native cleanup investigation](https://github.com/1298345095/wxnotepad/actions/runs/45743347624)
reused the failing Windows executable and Python 3.14.10. All 15 original and 15
modified caret attempts passed. Separate real `CreateFileW` holders verified
transient-lock recovery, persistent-lock failure at five seconds, and propagation
of the original body exception. An exited-process control permitted EXE deletion
with both handles open.

### Native shortcut completion

The X11 release helper used `wxYield()` inside an outer key-up/deadline loop.
Repeated native paints could keep that drain running after the real key-up had
already arrived. The helper now rechecks release and deadline between native
iterations. Window presentation has its own completion state: it tracks each
owned window's show epoch and actual `GDK_MAP` acknowledgement, including windows
shown later during command dispatch. This handles GTK's deferred show transition,
where wx's shown flag can precede native presentation.

The [normal-period investigation](https://github.com/1298345095/wxnotepad/actions/runs/45747432757)
completed all 14 sessions. At the measured 600 ms caret period, the original X11
first Save and first Find performed two to five paints after key-up inside the
same dispatch. The modified first Save, first Find and near-limit Save returned
from that dispatch with no further paint. All original search scenarios remained.

The [50 ms caret control](https://github.com/1298345095/wxnotepad/actions/runs/45749170020)
then reproduced continuous repaint inside the original first-Save drain: both
processes timed out at 60 seconds after more than 200 completed paints. The
finite-dispatch version completed its first three shortcuts, then both processes
timed out inside the final Find feedback after native map acknowledgement. These
observations separate release-drain completion from the nested modal response.

### File-readback cost under coverage

[Native phase measurements](https://github.com/1298345095/wxnotepad/actions/runs/45747432757)
located substantial coverage overhead in the large-file fixture's three
per-character readbacks. A fixture-local bulk reader reduced those measured
stages while preserving the complete 33,554,444-byte comparisons. That experiment
guided the shared `TestFiles::Bytes` implementation: it now reads 64 KiB blocks to
EOF, appends the final partial block, and reports a read error separately from EOF.
Reading the open stream to EOF also preserves callers polling child-process logs.

Binary, empty-file, block-boundary, missing-file, read-error and nonseekable-stream
contracts passed. The [formal Linux job](https://github.com/1298345095/wxnotepad/actions/runs/45748634143/job/139670736264)
passed both backend suites; `large_language_selection` completed in 1.42 seconds
on X11 and 1.33 seconds on Wayland. The improvement belongs to test readback;
production file loading and saving keep their existing responsibilities.

### Modal feedback response and retained pressure regression

The [modal-response comparison](https://github.com/1298345095/wxnotepad/actions/runs/45753054592)
recorded a queued but never-fired `CallAfter` in both 50 ms controls, followed by
60-second timeouts despite native map acknowledgement and real key-up. The shared
response helper now owns a one-shot timer and a weak reference to the opening
shortcut, requires its real release before clicking, and cancels expired work.
Native dispatch also rethrows exceptions stored by native callbacks on return.

All eight candidate sessions, covering 12 CTests, passed. The two X11 pressure
searches completed in 31.78–32.35 seconds; ordinary X11/Wayland searches, held-key
feedback and Escape also passed. All six candidate search traces recorded release
before Click and closed every span with zero dropped records. In all four X11
traces, the same feedback window's native Map acknowledgement preceded Click.

`SelectionLimit` retains the 50 ms caret period on X11 after `ResetStream`. It
keeps the 1 MiB input, all 83 assertions, 13 scenarios and 60-second limit. Each
scenario owns a fresh frame, so this pressure ends with scenario 7; Wayland keeps
its existing period. Those two measured pressure sessions completed within the
original 60-second limit; their remaining time describes that comparison.


### Native release priority under coverage

The next [formal X11 coverage run](https://github.com/1298345095/wxnotepad/actions/runs/45754274268/job/139689086449)
failed the second Alt+F3 release assertion in scenario 7 after 24.307 seconds.
Its original three-second input deadline expired; the 60-second CTest timeout
was not reached. That artifact had no X11 release/driver snapshot.

The [four-session observation](https://github.com/1298345095/wxnotepad/actions/runs/45758226385)
passed all eight searches across 50 ms and measured system-default 600 ms caret
periods. It did not reproduce the three-second failure. In seven near-limit Save
waits, the helper explicitly ran 449–485 ms of pending work before real key-up,
including one 201–217 ms paint. The actual selection-save function took only
4–5 ms. These measurements separate expensive pending work from the search
operation itself; they do not establish when input entered the original failed
process's native queue.

The helper appended full `ProcessPendingEvents()` and `ProcessIdle()` after
every native iteration, ahead of the next opportunity to dispatch input.
wxGTK already schedules both through its low-priority native idle source.
`DispatchX11Iteration()` now owns one GTK iteration and the existing boundary
for stored exceptions. The X11 release loop uses that operation, then retains the
original full final dispatch and actual Map acknowledgement after release.
Wayland keeps its compositor-transition path. The three-second input deadline,
60-second CTest limit, full 1 MiB scenario and 50 ms X11 pressure remain.

X11 now also uses the existing release-failure snapshot and driver-history
collection previously enabled only for Wayland. The snapshot remains on the
failure path and records the owned editor, focus, last key event and request.

The [candidate comparison](https://github.com/1298345095/wxnotepad/actions/runs/45759252340)
completed 12 sessions and 18 registered CTests, all with actual passing results.
The four controls also passed. All 14 candidate tests passed: four concurrent
X11 pressure searches and their held-key feedback checks, two Wayland searches
and feedback checks, and Escape on both backends. For the near-limit X11 Save,
real release arrived in 1.298–1.367 seconds in the controls and 0.724–0.786 seconds
in the candidates. Across all 16 measured candidate X11 shortcuts, explicit
pending/idle calls before real release were zero and every release arrived
within the original deadline. The original final dispatch still processes
pending work after release; window Map and modal response checks remain active.

## Verification

New behavior-focused coverage:

| Test | Contract |
| --- | --- |
| `feature_session_contracts` / `document_files.cpp` | Six real-file scenarios: cancellation cleanup, complete binary replacement, destination replacement failure, first-preparation timestamp retention, destination changes after a decision and Save Copy early creation. |
| `syntax_detection_contracts` | Twenty-four filename/content precedence, override, family/dialect and bounded-sniffing checks through the detection interface. |
| Existing word-wrap settings regression | Adds cancelled and failed Open cases while a file override is active, before checking persisted preference and New/reload behavior. |
| Existing dialog-layout regression | Uses the focused wrap-options value and continues exercising the real native dialog. |

Existing save/copy/backup/time, style import/theme, automatic insertion,
completion/IME, menu/shortcut and wrap regressions remain registered. New CTest
registration is appended after the existing groups so existing private-display
shard ordering is retained.

Local verification uses the canonical formatter, Python syntax checks,
the helper test suite and the pinned gettext extraction/catalog checks described
in `docs/ci.md`. Native compilation and Windows/X11/Wayland regressions use the
repository's configured GitHub Actions jobs; their exact results belong to the
PR check records.
