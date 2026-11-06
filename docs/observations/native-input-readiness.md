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
