# X11 activation setup requires native mapping

The main CI run for `e46f2110` failed only
`packaged_window_routing_activation_en` in its ordinary X11 suite:

https://github.com/1298345095/wxnotepad/actions/runs/45928354331/job/140274951657

The fixture had received the application's initial `shown=1` observation, then
immediately searched for a visible X11 window by PID. `xdotool` returned exit 1
with no match; the entire case ended after about one second. It never reached its minimize or
activation assertions. That run used wxWidgets 3.3.3#7; the separate 3.3.4 upgrade
is outside this fix.

## Reproduction and cause

The unchanged main fixture also failed locally on the second four-session
attempt, in the traditional Chinese activation case, with the same query error.
The application sources and executable were kept at main; the existing Linux
3.3.3 SDK was reused.

A controlled reproduction paused only the test's own private Openbox while the
real packaged application started. The application published `shown=1`,
`active=0`, `backend=GdkX11Display` before the WM processed its map request.
Failure-only read observations confirmed that the application remained alive:
the original query found no window, and a visible window appeared after the WM
resumed. The original failed query was still raised; those diagnostic reads did
not turn the attempt into a pass.

This establishes that wx's shown flag is insufficient for this native precondition.
The original hosted artifact did not retain this activation case's application
logs, so it cannot independently prove the historical process state at failure.

## Fix and regression

Before the original `windowminimize`, a read-only helper now waits for exactly one
visible window belonging to the application's PID. It uses a monotonic 15-second
setup budget, matching the existing application startup-observation budget.
Ordinary no-match results can settle; application exit, multiple windows and
native query errors fail. Each query is bounded by the remaining budget.

All existing minimize, requester exit, restored visibility and delivery checks
remain, as do the 120-second registered guards and default private-session
concurrency. Windows and Wayland activation behavior is unchanged. Application
code, dependency versions and coverage/duplication gates are unchanged.

`native_x11_routing_map_readiness` reproduces the gap using the actual packaged
application and complete original English activation scenario. It pauses only
the Openbox child owned by its managed private X11 session, observes startup,
and resumes mapping after a controlled 100 ms delay, including on failure.
Before the fix it failed with the same `CalledProcessError`; after the fix the
same registered regression passed. It is X11-specific and skips on Wayland.
Managed routing logs and this regression's startup observation are placed under
the existing retained `Testing/Temporary/` artifact path.

Final local validation passed all 16 selected X11 cases. Wayland passed the
original 15 closing/session/activation/forward-current/force-new cases across
English, simplified Chinese and traditional Chinese; the added X11-only case
skipped. Both used default concurrency. Python syntax and canonical C++
formatting checks passed. Required hosted workflows remain separate gates.

Local logs, the initial hosted artifact and red/green evidence are retained at
`F:/wxnotepad-evidence/main-ci-e46f2110-2026-11-26/` and
`/home/ctrls/wxnotepad-x11-activation-e46f2110-2026-11-26/`.
