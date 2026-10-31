# Actual-program resource observations (#20)

The retained measured run and comparison are in
[issue20-README.md](observations/issue20-README.md).

`tools/resources.py` launches the **actual Release wxnotepad** separately for
every row. It does not link another host or hide the editor. `--startup-file PATH`
opens through `EditorFrame::Open`. The optional `--observe DIRECTORY` enables a
10 ms timer in that same app; the normal app has no observer timer. The directory
must exist. This narrow experiment protocol accepts only numbered `open` and
`save` command files, invokes the same frame operations, and publishes state after
frame/editor `Update()` return. It avoids native chooser preparation in resource
timings; chooser behavior is covered separately by the file-operation tickets.

## Boundaries and repetitions

All timestamps are the Python observer's `perf_counter` seconds. Endpoints are
**observed state upper bounds**, including process launch, command-file creation,
timer delay, native input dispatch, subprocesses where used, and 10 ms polling.
Synchronous `Update()` return does not prove compositor presentation. One untimed
visual calibration per backend checks a drawn `X` in the actual frame.

* Startup: immediately before spawn to initial shown/update state and to native
  `X` acceptance (dirty state plus exactly one byte added). Five independent empty
  and five independent 4096-byte startup processes per backend. No idle delay is
  inserted into startup edit timing.
* Pristine idle: three separate clean processes per empty/small case; memory is
  read after one quiet second after shown/update state. Startup edit history is
  absent from these processes.
* Large files: three independent processes each for 10 and 100 MiB. Start with a
  shown empty frame. Open measures command dispatch to completed Open/update
  state, with the exact loaded length. Edit measures one native `X` at byte zero
  to dirty/update state, with exactly one byte added. Save measures command
  dispatch to successful Save/update and clean state. A one-second quiet interval
  precedes each opened/edited/saved memory snapshot.
* Windows input is `WM_CHAR` posted to the editor HWND after checking its PID.
  It uses the normal native character route without global input or forced focus;
  it does not measure physical keyboard or IME latency. Linux uses `xdotool` on a
  private Xvfb display, directly for X11 or through nested Weston for Wayland.
  Its focus/key subprocess overhead remains in the measured edit interval.
  Wayland startup waits for native frame activation before dispatching input.
  For the one reserved Windows batch, `--owned-topmost` raises only the launched
  frame above normal windows without activating or injecting global input.
  Its SetWindowPos/RedrawWindow calls remain in startup observation overhead.
* Memory: Windows `GetProcessMemoryInfo` WorkingSetSize and PrivateUsage;
  Linux target `/proc/PID/status` VmRSS. These are stage snapshots, not peaks,
  and the platform counters are not interchangeable.

The sample recipe is #7's 128-byte ASCII/LF line; `needle` replaces six bytes in
the final line of each MiB. Samples have no BOM. Files are generated and hashed
before the batch, and copied before each spawn. OS caches remain enabled and may
be warm, including the first run. Fresh processes do not imply cold storage.
The saved result is compared completely with `b"X" + original`, then hashed,
after all time/memory observations. Each raw row retains run order, PID, paths,
timestamps, states and failures. Medians exclude failed rows and retain counts;
the batch stops on a failure. There is no numerical performance gate.

## Reproduction

Build Release with `tools/build.py` as described in the project README. Commit
the implementation **before** measuring; pass that exact source commit and keep
the generated executable SHA-256. Use a new disposable output directory. Ensure
no other benchmark/build/GUI tests overlap the timed batch.

Windows (no global input):

```text
python -X utf8 tests/resource_process.py build/cmake/Release/wxnotepad.exe
python -X utf8 tools/resources.py build/cmake/Release/wxnotepad.exe build/resources-windows --source-commit COMMIT --owned-topmost
```

Linux, from a native Linux source/build/data directory, once per backend:

```sh
xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' dbus-run-session -- \
  python3 tools/clipboard_isolated.py x11 python3 tests/resource_process.py build/cmake/wxnotepad
xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' dbus-run-session -- \
  python3 tools/clipboard_isolated.py x11 python3 tools/resources.py \
  build/cmake/wxnotepad build/resources-x11 --source-commit COMMIT
```

Repeat with `wayland`. The helper runs full windows in private Xvfb, and for
Wayland uses nested Weston with Pixman. Record those conditions separately from
#7's WSLg desktop. Backend evidence is the actual GDK display object's type,
published by the app, rather than just `GDK_BACKEND`.

The process seam test first failed against the unchanged app (missing state
timed out), then passed after implementation. It checks shown startup-file state,
native edit acceptance, Save success and independent exact saved bytes. Linux
checks require the private session to keep input isolated from the shared desktop.

## Comparison boundary

#7 used five visible startup processes, small-file-at-launch, a first-idle
`SetFocus(); Update()` READY marker, and a 100 ms memory delay. Its large file
operations hid the control, timed read/SetText directly, inserted seven bytes near
the middle then undid, and did extra find/replace work. It did **not** time Save.
Here production Open also validates and constructs document state, editing stays
visible, memory waits one second, and Save extracts text and safely replaces the
file. Sample hashes/core revision can be compared directly. Timing or memory
differences are contextual observations with these boundary differences, not a
controlled identical-workload speedup. Search, 1 GiB, long-line stress and Linux
IME are outside this measurement.
