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
            weston = subprocess.Popen(["weston", "--backend=x11", "--renderer=pixman",
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
            if (request in ("editor", "peer", "gesture") or request.startswith(("key:", "focus:", "drag:", "front:", "place:"))) and time.monotonic() - last_switch > .25:
                def front(title):
                    if backend == "x11":
                        windows = xdo("search", "--onlyvisible", "--name", title).splitlines()
                        if not windows: raise RuntimeError("Owned drag window missing: " + title)
                        xdo("windowraise", windows[0])
                        xdo("windowfocus", "--sync", windows[0])
                    else:
                        xdo("key", "--clearmodifiers", "super+Tab")
                    time.sleep(.3)
                if request.startswith("place:"):
                    # Place the two owned Wayland transfer windows side by side
                    # using Weston's native move gesture and native local hits.
                    pointer_file, target_x, target_y = request[6:].rsplit(",", 2)
                    pointer = Path(pointer_file)
                    pointer.unlink(missing_ok=True)
                    geometry = xdo("getwindowgeometry", "--shell", weston_window)
                    values = dict(line.split("=", 1) for line in geometry.splitlines())
                    ox, oy = int(values["X"]), int(values["Y"])
                    hit = None
                    for px, py in ((500, 400), (800, 400), (300, 400), (1100, 400)):
                        xdo("mousemove", str(px + ox), str(py + oy))
                        time.sleep(.15)
                        if pointer.exists():
                            hit = tuple(map(int, pointer.read_text().split(",")))
                            break
                    if hit is None: raise RuntimeError("Owned Wayland peer did not receive native pointer probe")
                    xdo("keydown", "Super_L")
                    xdo("mousedown", "1")
                    time.sleep(.1)
                    xdo("mousemove", str(int(target_x) + hit[0] + ox), str(int(target_y) + hit[1] + oy))
                    time.sleep(.3)
                    xdo("mouseup", "1")
                    xdo("keyup", "Super_L")
                    focus.write_text("", encoding="utf-8")
                elif request.startswith("front:"):
                    front(request[6:])
                    focus.write_text("", encoding="utf-8")
                elif request.startswith("drag:"):
                    # A fullscreen owned fixture uses output-local coordinates
                    # on both X11 and the nested Weston output.
                    parts = request[5:].split(",")
                    x1, y1, x2, y2, modifier, cancel, pause = parts[:7]
                    switch = parts[7] if len(parts) > 7 else ""
                    origin = (0, 0)
                    if backend == "wayland":
                        geometry = xdo("getwindowgeometry", "--shell", weston_window)
                        values = dict(line.split("=", 1) for line in geometry.splitlines())
                        origin = (int(values["X"]), int(values["Y"]))
                    def move(x, y):
                        xdo("mousemove", str(int(x) + origin[0]), str(int(y) + origin[1]))
                    key = {"ctrl": "Control_L", "shift": "Shift_L"}.get(modifier)
                    move(x1, y1)
                    xdo("mousedown", "1")
                    time.sleep(.15)
                    move(int(x1) + 10, y1)
                    time.sleep(.2)
                    if key: xdo("keydown", key)
                    if switch: front("wxnotepad stream drag " + switch)
                    move(x2, y2)
                    time.sleep(int(pause) / 1000)
                    if int(cancel):
                        xdo("key", "Escape")
                        time.sleep(.3)
                        if backend == "wayland":
                            # Nested Weston withdraws keyboard focus throughout
                            # DnD. Cancel through a native rejected drop instead.
                            move(1500, 950)
                            time.sleep(.2)
                    xdo("mouseup", "1")
                    if key: xdo("keyup", key)
                    focus.write_text("", encoding="utf-8")
                elif request.startswith("focus:"):
                    if backend == "x11":
                        windows = xdo("search", "--onlyvisible", "--name", request[6:]).splitlines()
                        if not windows: time.sleep(.03); continue
                        focus.write_text("", encoding="utf-8")
                        xdo("windowfocus", "--sync", windows[0])
                    else:
                        focus.write_text("", encoding="utf-8")
                        xdo("windowfocus", "--sync", weston_window)
                elif request.startswith("key:"):
                    focus.write_text("", encoding="utf-8")
                    xdo("key", "--clearmodifiers", request[4:])
                elif request == "gesture":
                    # Native events can make the receiver publish its next
                    # request before xdotool exits. Claim this one first.
                    focus.write_text("", encoding="utf-8")
                    xdo("key", "--clearmodifiers", "Shift_L")
                elif backend == "x11":
                    windows = xdo("search", "--onlyvisible", "--name", "wxnotepad clipboard focus: " + request).splitlines()
                    if not windows: time.sleep(.03); continue
                    window = windows[0]
                    xdo("windowfocus", "--sync", window)
                else:
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
