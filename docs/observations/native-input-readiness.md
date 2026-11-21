# Native input readiness handoff

Issue: [#127](https://github.com/1298345095/wxnotepad/issues/127).

## Demonstrated failure

The retained pre-batch clipboard binaries failed the existing first-map readiness
fixture on a fresh private Wayland display in 3.90 seconds. Low-volume native
activation and key traces reproduced the same assertion. The peer received both
Shift press and release while the editor received neither.

The peer had taken compositor focus and answered `ready`. The editor process
still reported its cached active/focus state, published its `editor` focus request,
then immediately cleared it without dispatching the pending deactivate event.
The private driver therefore saw only the following `gesture` request and sent
that gesture to the peer. This was a fixture handoff failure before clipboard
ownership changed.

## Focus request ownership

Only the private driver now acknowledges an `editor` or `peer` focus request.
The editor waits for that acknowledgement and dispatches native events before
checking its active/focus state. The driver claims each request before issuing
native input, so it cannot clear a subsequent request published during that input.
The existing native Shift release is still required before clipboard operations.

An X11 four-session comparison exposed the corresponding peer producer race:
raising the peer could give it native focus before the driver claimed `peer`.
The peer then replaced that request with `gesture`; the driver's late claim
erased the gesture. A controlled 100 ms claim delay captured the exact transition
from `peer` to `gesture` before the claim, followed by the existing
`native gesture unavailable` failure. The peer now also waits for the focus claim
before publishing its gesture.

## Retained regression

`native_clipboard_peer_readiness` retains the 200 ms first-map workload and adds
`WXNOTEPAD_CLIPBOARD_DEFER_PEER_FOCUS_CLAIM=1`. In this fixture the peer publishes
`peer-deferred`, and the private driver waits 100 ms before claiming that owned
peer request. The peer acknowledgement correction removed from an otherwise
current comparison build fails with the original receipt assertion at 4.12
seconds. The corrected fixture uses the same workload and deadlines.

The delay applies to this readiness request. The three-second native focus and
gesture deadlines and the 30-second CTest timeout are unchanged. The regression
still uses native key events, native cross-process clipboard transfer, and the
independently saved clipboard bytes. Missing receipt and mismatched text retain
their assertions.

For a configured Linux build, the focused invocation is:

```sh
xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' \
  dbus-run-session -- python3 tools/clipboard_isolated.py wayland \
  ctest --test-dir "$build/cmake" -R '^native_clipboard_peer_readiness$' \
  --output-on-failure
```

## Shortcut comparison and evidence

Alt+P and Alt+L use their native key request/release seam directly; they do not
use the clipboard peer focus handoff. Both passed the focused individual checks
and the Wayland four-session checks. A final X11 four-session run failed the
same shortcut family's Shift+Alt+Z release assertion at a total CTest duration of
0.91 seconds, while readiness, system clipboard and Alt+P passed. That helper
uses a three-second `wxStopWatch` wait. The clipboard handoff evidence establishes
no shared cause for this separate shortcut observation.

Three bounded paired-clock/native-receipt probes in the same four-session X11
profile subsequently passed. One successful Ctrl+Shift+Alt+L wait measured
1499 ms with both `wxStopWatch` and `std::chrono::system_clock`, versus 336 ms
with `std::chrono::steady_clock`; the expected native release was received.
The installed wxWidgets 3.3.3 stopwatch uses UTC time on Linux. These probes
demonstrate a clock divergence, while the failing shortcut invocation has no
paired-clock capture. The shortcut failure remains a separate retained
observation, and the input handoff change leaves its clocks and assertions intact.

Exact environment, revision provenance, runnable comparison scripts, failing and
passing native transition logs, the regression mutation diff, and focused test
results are preserved on Windows at:

`E:/NewNotepad4/wxnotepad-evidence/native-test-stability-2026-11-06/127/`.

The uninstrumented X11 peer failure and the intermediate successful diagnostic
replays remain alongside the final comparisons. Final Windows and complete
hosted-matrix results are recorded by the integration delivery.

## Acknowledgement after a busy GUI callback

CI run `44897603406`, Wayland coverage at `89e5fe1`, failed search continuation
scenario 7/13 (`SelectionLimit`). Its failure history records a driver focus
acknowledgement about 4.17 seconds before the assertion. The failure's fresh read
also found an empty request, but the wait still held its earlier unacknowledged
state. The history does not identify the GUI callback that prevented polling.

A controlled native replay made a GUI callback outlast the original three-second
focus deadline. The driver acknowledged at 266 ms, the actual editor had focus,
and the original helper still failed. The permanent
`native_focus_acknowledgement` regression also failed before the correction.

Active GTK windows now publish `focus-until:<monotonic-expiry-us>:<title>`.
The driver checks expiry before native focus and immediately before publishing
its acknowledgement. Inactive Wayland activation retains its existing deadline;
inactive X11 activation retains its existing `front:` route. After a callback
outlasts polling, the helper reads the acknowledgement again only for these
deadline-gated requests, then retains the actual frame/editor focus checks.
The three-second deadline, native input route and failure assertion are unchanged.

The regression exercises both an acknowledgement received before expiry while
the GUI remains busy, and an expired request that the actual driver must reject.
It observes the driver's rejection event before checking that the request remains
unacknowledged. Removing either the fresh observation or the driver's deadline
rejection produces a failing native regression. X11 and Wayland also retain the
complete original 13-scenario search continuation test.

Evidence, red controls and native results are retained at
`F:/wxnotepad-evidence/toolbar-artwork-2026-11-21/focus-cache-control/`.
This correction does not establish the cause of the separately observed
scenario-13 key-release failure.

## Wayland file-drop fixture placement

CI run `44906515593` at `61dab58` passed X11 and both coverage backends, but
ordinary Wayland shard 4 stopped in `native_file_drop`. Its first file drag from
`860,102` to `849,225` never observed native target phase `target:2`. The
separate `native_stream_drag` case passed. The failed shard's CTest process was
terminated by the driver before its current test output was flushed.

The isolated file-drop case and the failed display's preceding 25 tests passed
locally. A native window-placement replay then reproduced the exact coordinates
and phase failure twice in about nine seconds: the intended editor point was
one pixel left of the peer widget, under its window decoration. Native tracing
observed source press/start but no target motion. Moving only Main 45 pixels
left preserved all four original file drags and their assertions and passed.
These are controlled native observations; the historical CI did not retain its
full scene graph or the target's event trace.

The fixture now verifies Main's native focus, moves it to the left of the private
display, raises and positions the peer, then reads their actual compositor and
widget coordinates again. It requires the editor and peer client areas to remain
visible before issuing the original file offers. The shared window-move helper
retains the existing native Super-plus-mouse route.

`native_file_drop_overlap` first deliberately places the editor's intended drop
point under the peer decoration and checks that precondition using actual widget
geometry. It then executes the same file-drop workflow. Before the layout
correction, this registered regression fails at the same native phase in 9.07
seconds. Afterward it passes, as does the original captured-coordinate replay.
X11 runs the complete original file-drop workflow for this additional case;
there is no additional skip.

The existing first-file-only Unicode/percent-path, read-only, directory chooser,
dirty-document Cancel/Save, disk-byte and undo assertions remain. The five-second
driver phase wait, eight-second caller wait, ninety-second CTest guard and default
parallel test policy are unchanged. No application drag/drop code is modified.
Replays, mutation diffs and native reports are retained at
`F:/wxnotepad-evidence/toolbar-artwork-2026-11-21/wayland-drag-followup/`.

## File chooser property publication

The first full local Wayland run at `6551630` failed packaged localization layout
while reading the Open button's role after setting the chooser location. A later
X11 layout run failed reading the filter combo's role after navigation. Both
reported the native `atspi_error`, `ATSPI_ERROR_IPC` code 1, `timeout from dbind`.
Neither failure was a rejected application action.

A controlled GTK publisher reproduces each blocked role query for 3.5 seconds
through the actual AT-SPI driver. The Open-query regression failed before the
correction; the extended filter-query regression also failed with the same native
error, before opening its popup. The publisher contains real entry, button and
combo widgets; it does not substitute for the actual packaged file chooser.

The driver retries only the two read-only chooser snapshots for this exact native
timeout. Open lookup uses the existing navigation deadline. Filter lookup, which
previously had no readiness loop, now has a ten-second bound. The one-second
native RPC timeout and the 180-second packaged-layout guard remain unchanged.
Text writes, Open activation and each filter-popup action remain outside the
retry handler. Unrecognized errors still propagate.

The registered `native_localization_chooser_readiness` case requires both property
stalls to occur and recover, one location write, one Open activation and exactly
one popup-open/popup-close pair. Both backends pass the native readiness cases
and the real packaged-layout test. Full ordinary Wayland passes all 258 registered
cases in the default 20 private sessions. Red controls and complete logs are in
the same evidence directory above. This correction does not establish the cause
of the separate coverage scenario-13 F3-release failure.

## Search feedback and the opening key release

CI run `44920255937` at `6551630` passed ordinary Linux and Windows, but Wayland
coverage failed `FeedbackTransitions` (scenario 13/13). The driver had injected
the key; the observer never received F3-up and last saw Control-up. Historical
driver records identify only `key`, not which of the three feedback shortcuts
was active. The accompanying AT-SPI warning alone does not establish a cause.

The isolated feedback scenario passed twenty ordinary-input controls. Scanning
native key durations then reproduced the same release failure with a 16 ms
xdotool delay. GTK/wx tracing showed the second feedback auto-response completing
before F3-up, with no later F3 release delivered to a widget. A 150 ms held-key
regression reliably exposes the early-response ordering even when release later
reaches the editor. Before the fix it fails in 2.53 seconds with
`feedback responses must follow the opening shortcut's native release`.

The feedback fixture now observes the opening shortcut's actual release before
clicking its response. It uses the existing shortcut deadline, published after
the input request is written, instead of starting another three-second wait when
the modal opens. On expiry it unwinds the modal so the original release assertion
can fail. The Windows shortcut remains synchronous and does not wait for a
physical release. Application search and feedback code are unchanged.

`native_search_feedback_release` selects the feedback scenario with held native
input. Its explicit `key-delay:<milliseconds>:<keys>` driver request leaves normal
`key:` delivery unchanged. The full original thirteen-scenario case, all search
messages, ranges and settings assertions, the three-second release budget and
the sixty-second CTest guard remain. Controlled traces, failed setup attempts and
the registered red regression are retained in the evidence directory above;
only `feedback-permanent-red-05` is the completed held-input red acceptance run.

## Follow-up hosted popup failure

At `b6aba85`, all three hosted Linux test jobs failed the new chooser regression
at `popup-closed`, while ordinary hosted Wayland and the original search cases
passed. Sonar did not scan because coverage failed. Local full runs and a replay
of the failed display's preceding 63 cases had passed; those results did not
establish that the new popup observation was safe across native timing.

A native ATK-action publisher that maps its real GTK menu 250 ms after the first
action reproduces the same failure. Both action receipts observe the menu hidden
and request Open; only one show transition occurs, with no close. The registered
`native_localization_chooser_popup_readiness` reproduces this before the fix in
17.42 seconds. The historical CI did not retain its provider's property state,
so that state is a controlled reproduction, not a captured hosted observation.

The driver now waits for the combo's actual accessible popup to be SHOWING after
one Open action, then waits for it to be hidden after one Close action. Role and
popup snapshots share the filter readiness deadline; actions are never retried.
The immediate native-accessible case, both original popup property assertions,
the exact action pair and all 30/180-second CTest guards remain. The delayed case
adds two explicit action receipts and uses the existing publication-delay
constant. Both backends pass the original/new regressions and real packaged
layout after the correction. Failure output now retains provider messages and
the relevant receipts.

## Windows child-record publication

The same hosted run failed `original_document_tools` at `child record is readable`
and then its original 55-second process guard. It did not retain a native error
code. An owned native control separately reproduces a concrete defect in that
reader: after rename publishes a destination but before its DELETE-access handle
closes, the CRT open used by wxFFile fails with Win32 error 32.

Document-tool records now reuse the immutable-publication reader already used by
Explorer acknowledgements, renamed `NativePublishedFile` for the two consumers.
It shares DELETE access, reads exact bytes, and preserves native error details.
Read failures still fail the original readable assertion, without leaving a
deferred wxLogGui dialog. The ten-second publication wait and exact argument,
directory and disk-byte assertions remain. The existing retained-rename-handle
regression and original document-tool/Explorer workflows pass. This addresses a
reproduced sharing defect; a fresh hosted run is still needed to determine
whether it accounts for the historical failure.
