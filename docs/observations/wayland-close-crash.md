# Wayland unsaved-close crash investigation (#128)

Disposition: **unresolved; original SIGSEGV unreproduced**. The change adds
lifecycle evidence to the existing close fixture. Successful repetitions do
not demonstrate a correction to the original crash.

## Original failure reconstructed

Hosted run [42253287264, job 128013712625](https://github.com/1298345095/wxnotepad/actions/runs/42253287264/job/128013712625?pr=125)
ran PR head `d92f696319b8e15b954bafb79a9817252cb6c4d9` through GitHub's synthetic
merge `64be73963c69b240715b0b44bba7484b454519cb`, onto main `14e6330`.
The retained log records Ubuntu 24.04.5, runner image `20261101.360.1`, GCC
13.3.0, Python 3.12.16, vcpkg wxWidgets 3.3.3#1 and GTK 3.24.52#2 with Wayland.

The exact outer command was:

```sh
python -X utf8 .github/scripts/ci.py test --backend wayland
```

It selected 97 tests in four concurrent private sessions, each running CTest
serially. `close_unsaved` was test 54 in shard 2, whose command used
`xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' dbus-run-session --`
and `tools/clipboard_isolated.py wayland ctest`, selecting 24 tests including
`close_unsaved`. The registered executable was `.ci/build/cmake/close_smoke`
with a 30-second CTest timeout. The entire original shard command is retained
verbatim in the evidence file `original-command.txt`.

The observed failure was `Exception: SegFault 3.50 sec`, at
`2026-11-06T16:40:36.3820745Z`. The only captured application output was an
AT-SPI service warning; buffered PASS output did not identify the last phase.
There was no core or matching close executable. The preceding X11 invocation
passed in 4.93 seconds. Fifty earlier local repeats, PR #125's final hosted
run, and the subsequent main-merge hosted run did not reproduce this SIGSEGV.

These facts establish neither a failing Save/Discard/Cancel branch nor an
application, test-driver or dependency lifetime as its cause. Closed #93's
observation-process exit timeout has a different failure signature.

## Added observation

The existing `close_unsaved` fixture remains the behavior seam. It retains all
assertions for native confirmations and choosers, modification state,
document identity, continued editing, undo/save point and exact saved bytes.
The 100 ms dialog timer, modal responses, frame deletion requests, event-loop
exit and 30-second timeout retain their original behavior.

Flushed `CLOSE-LIFECYCLE` lines add sequence numbers and monotonic elapsed
times for frame creation, close calls and their return values, native driver
callback entry/exit, confirmation response and chooser selection/acceptance,
destroy requests/events, `OnRun` return, `OnExit` and application destructor
entry. Existing PASS lines are unbuffered. A destroy event identifies entry
into destruction; the destructor marker likewise identifies its body rather
than completion of all base destruction. CTest retains the actual process
result. Frame addresses associate these boundaries without reading a frame
after a successful close. The optional synchronous driver observer uses a
static function and is absent in other file-command fixtures.

The local wxWidgets 3.3.3 sources clarify the observed boundaries:
`wxWindowBase::Close()` dispatches a vetoable close event; the application's
handler confirms changes and then calls `Destroy()`. In
`src/common/toplvcmn.cpp`, `wxTopLevelWindowBase::Destroy()` normally queues
the frame for deferred deletion; `DeletePendingObjects()` in
`src/common/appbase.cpp` deletes queued objects. `src/gtk/timer.cpp` calls
`Notify()` inside the native timer callback. `src/gtk/filedlg.cpp` enters the
native chooser's modal loop and releases its native object during dialog
destruction. This is a lifecycle map for interpreting a future stack, not
evidence that one of those operations caused the original SIGSEGV.

## Bounded experiment

Investigation base: `af3acfc7d49de88d004907a82872160ea6a472ef`.
Only the changed close test translation unit was rebuilt, with debug lines,
against the retained unchanged application/editor archives from PR #125.
The application sources and CMake definition at retained final head
`748d638` and the investigation base compare identically. The exact compile
and link commands and matching binary are preserved.

The local environment is Ubuntu 24.04 WSL2, GCC 13.3.0 and Weston 13.0.0.
Retained static vcpkg dependencies are wxWidgets 3.3.3#1 and GTK 3.24.52#2;
the system `pkg-config` GTK version is not the linked GTK version. Every
invocation owns a private display and D-Bus session. No browser or screenshot
is involved. The normal profile uses one serial close invocation at a time
with no concurrent compilation/load from the other investigation. This
isolates the close lifecycle; it does not replay the original four-shard
workload. The load profile pins the complete private session to CPU 0 with
four CPU-burning processes on that same CPU. A process/affinity snapshot
confirms all four burners were live and restricted to CPU 0.

The retained `run-close.py` records each complete command, CTest output,
JUnit result and display log. Its loop permits at most 20 close invocations
per Wayland profile, stops at the first native crash, and invokes the existing
`collect_cores.py` on that failure. It does not retry failures or change their
exit results. The command shape is:

```sh
ulimit -c unlimited
python3 /mnt/e/NewNotepad4/wxnotepad-evidence/native-test-stability-2026-11-06/128/run-close.py normal
python3 /mnt/e/NewNotepad4/wxnotepad-evidence/native-test-stability-2026-11-06/128/run-close.py load
python3 /mnt/e/NewNotepad4/wxnotepad-evidence/native-test-stability-2026-11-06/128/run-close.py x11
```

An initial Git Bash path-conversion error set an invalid Windows-shaped
`core_pattern` destination. The first 16 normal passes preceded detection.
The path was corrected during the remaining normal invocations; individual
normal processes were not tagged with the cutover time, so the normal
profile is not relied on as a verified kernel-capture run. All its process
results remain valid and are retained. No extra repetitions replaced them.
The load profile started after verifying the literal Linux pattern
`/home/ctrls/wxnotepad-close-128/evidence/cores/core.%p.%E` and directory
writability. `MSYS_NO_PATHCONV=1` prevents that conversion in the corrected
host command.

Before the load experiment, an intentionally crashing native probe exercised
that actual kernel destination. It exited with SIGSEGV (`-11`), and the
existing collector retained its core, matching executable and all-thread GDB
report identifying `owned_ci_crash` at `probe.cpp:2`. This probe verifies
capture plumbing and is stored separately from close-test results.

## Results and next action

| Backend/profile | Invocations | Passed | Native crashes | Fixture time |
| --- | ---: | ---: | ---: | --- |
| Wayland, normal | 20 | 20 | 0 | 4.75–4.82 s |
| Wayland, CPU 0 controlled load | 20 | 20 | 0 | 6.75–7.35 s |
| X11, no external load | 1 | 1 | 0 | 4.80 s |

All 41 logs retain the final document/history assertion, `OnRun` return,
`OnExit` and application destructor marker, and all actual CTest exits are 0.
Neither the original SIGSEGV nor the separate GTK pathbar assertion occurred.
The capture probe's intentional SIGSEGV is excluded from these close results.
The instrumentation flushes output synchronously and can perturb timing;
these repetitions do not reproduce every hosted workload or establish a fix.
Formatting and whitespace checks passed. Affected Windows close validation
belongs to the final integration checks.

The remaining untested hypotheses are a driver callback/modal-object lifetime,
deferred frame/editor destruction, and dependency work during chooser or
process teardown. A future stack must distinguish them. Their falsifiable
predictions are, respectively, a crash before callback exit/within response
dispatch; a crash in queued window/editor destruction; or a dependency stack
after chooser acceptance/close return or event-loop exit. None currently has
a captured original-failure stack.

The next action is to retain this instrumentation and the PR #125 core
collector for the next four-session hosted failure. Inspect the matching
binary/core and last completed marker before changing lifetime behavior;
then minimize the observed native path into a failing/passing regression.
There is no demonstrated crash fix or crash regression test to claim here.
The previously observed controlled-load GTK pathbar assertion remains a
separate observation: no stack or lifecycle evidence links it to the original
SIGSEGV.

Windows evidence root:
`E:/NewNotepad4/wxnotepad-evidence/native-test-stability-2026-11-06/128`.
It preserves the scripts, binary, exact source/commands, dependency records,
per-invocation logs, results and the probe's core/matching executable/stack
before Linux cleanup. The original core pattern is restored after experiments.
