"""Run native clipboard checks inside xvfb-run + dbus-run-session.

Usage: xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' \
       dbus-run-session -- python3 tools/clipboard_isolated.py wayland ctest ...
All focus/input stays on xvfb-run's private DISPLAY. No WSLg/Windows bridge.
Set WXNOTEPAD_DISPLAY_LOG_DIR to retain the full private Weston log.
"""
import os
from pathlib import Path
import signal
import shutil
import re
import subprocess
import sys
import tempfile
import time

backend, *command = sys.argv[1:]
if backend not in ("x11", "wayland") or not command:
    raise SystemExit("Expected x11|wayland followed by a test command")
if os.environ.get("DISPLAY") in (None, ":0", ":0.0") or not os.environ.get("XAUTHORITY"):
    raise SystemExit("Run inside xvfb-run with its private DISPLAY and XAUTHORITY")

def stop(process):
    if process is None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()

def xdo(*args):
    result = subprocess.run(["xdotool", *args], env=environment,
                            capture_output=True, text=True, timeout=2)
    if args[0] != "search":
        result.check_returncode()
    return result.stdout.strip()

def acknowledge(focus):
    # Readers may already have the request open. Replace it so their ReadAll
    # observes a stable snapshot while new readers observe completion.
    pending = focus.with_suffix(".ack")
    pending.write_text("", encoding="utf-8")
    pending.replace(focus)

with tempfile.TemporaryDirectory(prefix="wxnotepad-issue12-display-") as folder:
    runtime = Path(folder)
    environment = dict(os.environ, XDG_RUNTIME_DIR=folder, GDK_BACKEND=backend,
                       WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS=str(runtime / "focus"))
    environment.pop("WAYLAND_DISPLAY", None)
    environment.pop("WAYLAND_SOCKET", None)
    weston = test = None
    log = open(runtime / "weston.log", "w", encoding="utf-8")
    try:
        weston_window = None
        if backend == "wayland":
            weston = subprocess.Popen(["weston", "--debug", "--backend=x11", "--renderer=pixman",
                "--no-config", "--socket=wxnotepad-issue12", "--idle-time=0",
                "--width=1600", "--height=1000"], env=environment, stdout=log,
                stderr=subprocess.STDOUT, start_new_session=True)
            deadline = time.monotonic() + 8
            while not (runtime / "wxnotepad-issue12").exists():
                if weston.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("Private Weston did not start")
                time.sleep(.05)
            window = None
            while window is None and time.monotonic() < deadline:
                window = re.search(r"x11 output .* window id (\d+)", (runtime / "weston.log").read_text(encoding="utf-8"))
                if window is None: time.sleep(.05)
            if window is None: raise RuntimeError("Private Weston X window was not published")
            weston_window = window.group(1)
            xdo("windowfocus", "--sync", weston_window)
            environment["WAYLAND_DISPLAY"] = "wxnotepad-issue12"
        print(f"Private native {backend}: DISPLAY={environment['DISPLAY']}; runtime mode={oct(runtime.stat().st_mode & 0o777)}", flush=True)
        test = subprocess.Popen(command, env=environment, start_new_session=True)
        last_switch = 0.0
        while test.poll() is None:
            focus = runtime / "focus"
            request = focus.read_text(encoding="utf-8") if focus.exists() else ""
            if (request in ("editor", "peer", "peer-deferred", "gesture", "geometry") or request.startswith(("key:", "focus:", "drag:", "front:", "move-peer:"))) and time.monotonic() - last_switch > .25:
                if request == "peer-deferred":
                    # The readiness fixture holds a claim long enough to expose
                    # a receiver that replaces its focus request with a gesture.
                    time.sleep(.1)
                    request = "peer"
                def front(title):
                    if backend == "x11":
                        windows = xdo("search", "--onlyvisible", "--name", title).splitlines()
                        if not windows: raise RuntimeError("Owned drag window missing: " + title)
                        xdo("windowraise", windows[0])
                        xdo("windowfocus", "--sync", windows[0])
                    else:
                        xdo("key", "--clearmodifiers", "super+Tab")
                    time.sleep(.3)
                if request == "geometry":
                    acknowledge(focus)
                    # Read only the two owned drag fixture surfaces. Wayland
                    # clients cannot query their global compositor positions.
                    mapping_deadline = time.monotonic() + 3
                    while True:
                        scene = subprocess.run(["weston-debug", "scene-graph"], env=environment,
                                               capture_output=True, text=True, timeout=2, check=True).stdout
                        positions = {name: (int(x), int(y)) for name, x, y in re.findall(
                            r"top-level window 'wxnotepad stream drag (main|peer)'[^\n]*\n\s*position: \((-?\d+), (-?\d+)\)", scene)}
                        if set(positions) == {"main", "peer"}: break
                        if time.monotonic() > mapping_deadline:
                            raise RuntimeError("Private compositor must map both owned drag surfaces")
                        time.sleep(.05)
                    response = runtime / "geometry"
                    pending = response.with_suffix(".tmp")
                    pending.write_text(",".join(map(str, (*positions["main"], *positions["peer"]))), encoding="utf-8")
                    pending.replace(response)
                elif request.startswith("move-peer:"):
                    x1, y1, x2, y2 = map(int, request[10:].split(","))
                    geometry = xdo("getwindowgeometry", "--shell", weston_window)
                    values = dict(line.split("=", 1) for line in geometry.splitlines())
                    ox, oy = int(values["X"]), int(values["Y"])
                    xdo("mousemove", str(x1 + ox), str(y1 + oy))
                    xdo("keydown", "Super_L")
                    xdo("mousedown", "1")
                    time.sleep(.15)
                    xdo("mousemove", str(x2 + ox), str(y2 + oy))
                    time.sleep(.2)
                    xdo("mouseup", "1")
                    xdo("keyup", "Super_L")
                    acknowledge(focus)
                elif request.startswith("front:"):
                    front(request[6:])
                    acknowledge(focus)
                elif request.startswith("drag:"):
                    # A fullscreen owned fixture uses output-local coordinates
                    # on both X11 and the nested Weston output.
                    parts = request[5:].split(",")
                    x1, y1, x2, y2, modifier, cancel, pause = parts[:7]
                    switch = parts[7] if len(parts) > 7 else ""
                    gesture = parts[8]
                    action = parts[9]
                    def wait_drag_state(suffix, phase):
                        response = Path(str(focus) + suffix)
                        expected = f"{gesture}:{phase}"
                        deadline = time.monotonic() + 5
                        while not response.exists() or response.read_text(encoding="utf-8") != expected:
                            if test.poll() is not None or time.monotonic() > deadline:
                                raise RuntimeError(f"Native drag gesture={gesture} from={x1},{y1} to={x2},{y2} "
                                                   f"must observe native phase {phase}")
                            time.sleep(.01)
                    origin = (0, 0)
                    if backend == "wayland":
                        geometry = xdo("getwindowgeometry", "--shell", weston_window)
                        values = dict(line.split("=", 1) for line in geometry.splitlines())
                        origin = (int(values["X"]), int(values["Y"]))
                    def move(x, y):
                        xdo("mousemove", str(int(x) + origin[0]), str(int(y) + origin[1]))
                    key = {"ctrl": "Control_L", "shift": "Shift_L"}.get(modifier)
                    phase = Path(str(focus) + ".drag-phase")
                    pending = phase.with_suffix(".tmp")
                    move(x1, y1)
                    xdo("mousedown", "1")
                    try:
                        wait_drag_state(".drag-pressed", "start")
                        move(int(x1) + 10, y1)
                        wait_drag_state(".drag-started", "start")
                        if key: xdo("keydown", key)
                        if switch: front("wxnotepad stream drag " + switch)
                        pending.write_text(f"{gesture}:target", encoding="utf-8")
                        pending.replace(phase)
                        move(x2, y2)
                        wait_drag_state(".drag-motion", f"target:{action}")
                        if action != "0":
                            wait_drag_state(".drag-action", f"target:{action}")
                        time.sleep(int(pause) / 1000)
                        if int(cancel):
                            xdo("key", "Escape")
                            time.sleep(.3)
                            if backend == "wayland":
                                # Nested Weston withdraws keyboard focus throughout
                                # DnD. Cancel through a native rejected drop instead.
                                move(1500, 950)
                                time.sleep(.2)
                    finally:
                        xdo("mouseup", "1")
                        if key: xdo("keyup", key)
                        pending.write_text(f"{gesture}:released", encoding="utf-8")
                        pending.replace(phase)
                    wait_drag_state(".drag-ended", "end")
                    acknowledge(focus)
                elif request.startswith("focus:"):
                    if backend == "x11":
                        windows = xdo("search", "--onlyvisible", "--name", request[6:]).splitlines()
                        if not windows: time.sleep(.03); continue
                        acknowledge(focus)
                        xdo("windowfocus", "--sync", windows[0])
                    else:
                        acknowledge(focus)
                        xdo("windowfocus", "--sync", weston_window)
                elif request.startswith("key:"):
                    acknowledge(focus)
                    xdo("key", "--clearmodifiers", request[4:])
                elif request == "gesture":
                    # Native events can make the receiver publish its next
                    # request before xdotool exits. Claim this one first.
                    acknowledge(focus)
                    xdo("key", "--clearmodifiers", "Shift_L")
                elif backend == "x11":
                    windows = xdo("search", "--onlyvisible", "--name", "wxnotepad clipboard focus: " + request).splitlines()
                    if not windows: time.sleep(.03); continue
                    window = windows[0]
                    acknowledge(focus)
                    xdo("windowfocus", "--sync", window)
                else:
                    # Claim the focus request before native events can publish
                    # the next gesture, as for key and gesture requests above.
                    acknowledge(focus)
                    xdo("windowfocus", "--sync", weston_window)
                    xdo("key", "--clearmodifiers", "super+Tab")
                last_switch = time.monotonic()
            time.sleep(.03)
        result = test.returncode
    finally:
        stop(test)
        stop(weston)
        log.close()
        if os.environ.get("WXNOTEPAD_DISPLAY_LOG_DIR"):
            artifacts = Path(os.environ["WXNOTEPAD_DISPLAY_LOG_DIR"])
            artifacts.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(runtime / "weston.log", artifacts / f"{backend}-{os.getpid()}-weston.log")
        if weston is not None:
            print((runtime / "weston.log").read_text(encoding="utf-8")[-1500:], flush=True)
    raise SystemExit(result)
