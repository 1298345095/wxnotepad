"""Run native clipboard checks inside xvfb-run + dbus-run-session.

Usage: xvfb-run -a -s '-screen 0 1600x1000x24 -nolisten tcp' \
       dbus-run-session -- python3 tools/clipboard_isolated.py wayland ctest ...
All focus/input stays on xvfb-run's private DISPLAY. No WSLg/Windows bridge.
Set WXNOTEPAD_DISPLAY_LOG_DIR to retain the full private Weston log.
"""

import os
import json
from pathlib import Path
import signal
import shutil
import re
import subprocess
import sys
import tempfile
import time
import threading
from collections import deque
from weston_scene import input_ready_toplevel, mapped_toplevel, mapped_toplevel_position

WESTON_LOG = "weston.log"
SWITCH_WINDOW_KEY = "super+Tab"
FAILURE_HISTORY_EVENTS = 256
FOCUS_UNTIL_PREFIX = "focus-until:"
FRONT_UNTIL_PREFIX = "front-until:"
KEY_DELAY_PREFIX = "key-delay:"
X11_WINDOW_PREFIX = "x11-window:"

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
    driver_trace(runtime / "focus", "driver.native-begin", args[0])
    result = subprocess.run(["xdotool", *args], env=environment, capture_output=True, text=True, timeout=2)
    driver_trace(runtime / "focus", "driver.native-complete", f"{args[0]}:{result.returncode}")
    if args[0] != "search":
        result.check_returncode()
    return result.stdout.strip()


def acknowledge(focus, deadline_us=None):
    # Readers may already have the request open. Replace it so their ReadAll
    # observes a stable snapshot while new readers observe completion.
    pending = focus.with_suffix(".ack")
    pending.write_text("", encoding="utf-8")
    if deadline_us is not None and time.monotonic_ns() // 1000 >= deadline_us:
        return False
    pending.replace(focus)
    driver_trace(focus, "driver.ack")
    return True


def focus_owned_window(focus, title, deadline_us=None, *, raise_window=False):
    # Reject expired requests before native input; acknowledge rechecks after
    # X11's synchronous focus call and immediately before publishing the claim.
    if deadline_us is not None and time.monotonic_ns() // 1000 >= deadline_us:
        driver_trace(focus, "driver.expired")
        return True
    if backend == "x11":
        if title.startswith(X11_WINDOW_PREFIX):
            # A title may change while native document notifications settle.
            # Resolve the exact owned XID, retaining the native mapping check.
            target = title[len(X11_WINDOW_PREFIX) :]
            candidates = xdo("search", "--onlyvisible", "--class", ".").splitlines()
            windows = [target] if target in candidates else []
        else:
            windows = xdo("search", "--onlyvisible", "--name", title).splitlines()
        if not windows and not title.startswith(X11_WINDOW_PREFIX):
            # GTK omits legacy WM_NAME for some non-Latin titles. xdotool's
            # search then misses the visible window although getwindowname
            # reads its UTF-8 _NET_WM_NAME correctly.
            candidates = xdo("search", "--onlyvisible", "--class", ".").splitlines()
            windows = [window for window in candidates if re.search(title, xdo("getwindowname", window))]
        driver_trace(focus, "driver.focus-search", f"title={title};windows={','.join(windows)}")
        if not windows:
            return False
        if deadline_us is not None and time.monotonic_ns() // 1000 >= deadline_us:
            driver_trace(focus, "driver.expired")
            return True
        if raise_window:
            xdo("windowraise", windows[0])
        xdo("windowfocus", "--sync", windows[0])
    if not acknowledge(focus, deadline_us):
        driver_trace(focus, "driver.expired")
    elif backend != "x11":
        xdo("windowfocus", "--sync", weston_window)
    return True


def retain_activation_failure(process, surface, expiry, scene, matched, queries, timed_out):
    """Retain only observations already made by an unacknowledged activation."""
    directory = os.environ.get("WXNOTEPAD_DISPLAY_LOG_DIR")
    if not directory:
        return
    prefix = Path(directory) / f"wayland-{os.getpid()}-activation-{int(process)}-{int(surface)}-{int(expiry)}"
    try:
        prefix.parent.mkdir(parents=True, exist_ok=True)
        # The expired request can still be present on the next driver iteration.
        # Keep its first failure, including the last scene actually read for it.
        if prefix.with_suffix(".json").exists():
            return
        if scene is not None:
            prefix.with_suffix(".scene.txt").write_text(scene, encoding="utf-8")
        prefix.with_suffix(".json").write_text(
            json.dumps(
                {
                    "request_kind": "activate",
                    "acknowledged": False,
                    "owned_pid": int(process),
                    "owned_surface": int(surface),
                    "deadline_monotonic_us": int(expiry),
                    "last_scene_matched": matched,
                    "scene_queries": queries,
                    "scene_query_timed_out": timed_out,
                    "scene_file": prefix.with_suffix(".scene.txt").name if scene is not None else None,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        print(f"Native activation failure observation: {prefix.with_suffix('.json')}", file=sys.stderr, flush=True)
    except OSError as error:
        print(f"Could not retain native activation failure observation: {error}", file=sys.stderr, flush=True)


trace_gesture = "0"
trace_sequence = 0
trace_started = 0
trace_failed = False
failure_history = deque(maxlen=FAILURE_HISTORY_EVENTS)
failure_sequence = 0
request_kind = ""
read_sequence = 0


def driver_trace(focus, event, kind=""):
    """Keep boundary history in memory; persist untagged events only on failure."""
    global trace_sequence, trace_started, trace_failed, failure_sequence
    kind = kind or request_kind
    observed = time.monotonic_ns() // 1000
    failure_sequence += 1
    failure_history.append(
        (
            observed,
            f"mono_us={observed}\tpid={os.getpid()}\ttid={threading.get_native_id()}"
            f"\tseq={failure_sequence}\tevent={event}\tgesture={trace_gesture}\treadSeq={read_sequence}\tkind={kind}\n",
        )
    )
    if trace_gesture == "0" or trace_failed:
        return

    def row(name, fields):
        global trace_sequence
        trace_sequence += 1
        return (
            f"us={(time.monotonic_ns() - trace_started) // 1000}\tpid={os.getpid()}"
            f"\ttid={threading.get_native_id()}\tseq={trace_sequence}\tevent={name}\tgesture={trace_gesture}{fields}\n"
        )

    try:
        path = Path(str(focus) + ".diagnostics")
        first = not path.exists()
        if first:
            trace_sequence = 0
            trace_started = time.monotonic_ns()
        with path.open("a", encoding="utf-8") as output:
            if first:
                output.write(
                    row(
                        "header",
                        f"\tbuild=private-input-driver\tbackend={backend}"
                        f"\twx=not-loaded\tcore=not-loaded\tcompiler=Python-{sys.version.split()[0]}",
                    )
                )
            output.write(row(event, f"\tkind={kind}" if kind else ""))
            output.flush()
    except OSError:
        trace_failed = True
        print("Could not write native driver diagnostic log", file=sys.stderr, flush=True)


def retain_focus_failure(focus):
    marker = focus.with_suffix(".failure")
    directory = os.environ.get("WXNOTEPAD_DISPLAY_LOG_DIR")
    if not directory or not marker.exists():
        return
    try:
        snapshot = marker.read_text(encoding="utf-8")
        failed_at = int(re.search(r"\bmono_us=(\d+)", snapshot).group(1))
        process = int(re.search(r"\bownedPid=(\d+)", snapshot).group(1))
        destination = Path(directory) / f"{backend}-{os.getpid()}-focus-failure-{process}-{failed_at}.log"
        destination.parent.mkdir(parents=True, exist_ok=True)
        before = "".join(row for stamp, row in failure_history if stamp <= failed_at)
        after = "".join(row for stamp, row in failure_history if stamp > failed_at)
        # A native call may complete only after the fixture's deadline. Keep its
        # unmatched begin at the failure instant, then the actual later events.
        destination.write_text(
            before + snapshot + after + f"mono_us={time.monotonic_ns() // 1000}\tevent=driver.failure-observed\n",
            encoding="utf-8",
        )
        marker.unlink()
        print(f"Native focus failure history: {destination}", file=sys.stderr, flush=True)
    except OSError as error:
        print(f"Could not retain native focus failure history: {error}", file=sys.stderr, flush=True)


with tempfile.TemporaryDirectory(prefix="wxnotepad-issue12-display-") as folder:
    runtime = Path(folder)
    environment = dict(
        os.environ,
        XDG_RUNTIME_DIR=folder,
        GDK_BACKEND=backend,
        WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS=str(runtime / "focus"),
    )
    environment.pop("WAYLAND_DISPLAY", None)
    environment.pop("WAYLAND_SOCKET", None)
    weston = test = None
    log = open(runtime / WESTON_LOG, "w", encoding="utf-8")
    try:
        weston_window = None
        if backend == "wayland":
            weston = subprocess.Popen(
                [
                    "weston",
                    "--debug",
                    "--backend=x11",
                    "--renderer=pixman",
                    "--no-config",
                    "--socket=wxnotepad-issue12",
                    "--idle-time=0",
                    "--width=1600",
                    "--height=1000",
                ],
                env=environment,
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            deadline = time.monotonic() + 8
            while not (runtime / "wxnotepad-issue12").exists():
                if weston.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("Private Weston did not start")
                time.sleep(0.05)
            window = None
            while window is None and time.monotonic() < deadline:
                window = re.search(r"x11 output .* window id (\d+)", (runtime / WESTON_LOG).read_text(encoding="utf-8"))
                if window is None:
                    time.sleep(0.05)
            if window is None:
                raise RuntimeError("Private Weston X window was not published")
            weston_window = window.group(1)
            xdo("windowfocus", "--sync", weston_window)
            environment["WAYLAND_DISPLAY"] = "wxnotepad-issue12"
        print(
            f"Private native {backend}: DISPLAY={environment['DISPLAY']}; runtime mode={oct(runtime.stat().st_mode & 0o777)}",
            flush=True,
        )
        test = subprocess.Popen(command, env=environment, start_new_session=True)
        last_switch = 0.0
        last_read = None
        while test.poll() is None:
            focus = runtime / "focus"
            retain_focus_failure(focus)
            request = focus.read_text(encoding="utf-8") if focus.exists() else ""
            trace_gesture = "0"
            if request.startswith("trace:"):
                _, identity, request = request.split(":", 2)
                trace_gesture = identity if identity.isdecimal() else "0"
            request_kind = request.split(":", 1)[0] if request else "empty"
            if request != last_read:
                if request:
                    read_sequence += 1
                driver_trace(focus, "driver.read")
                last_read = request
            if (
                request in ("editor", "peer", "peer-deferred", "gesture", "geometry")
                or request.startswith(
                    (
                        "key:",
                        KEY_DELAY_PREFIX,
                        "keydown:",
                        "keyup:",
                        "click:",
                        "tree-drag:",
                        "focus:",
                        FOCUS_UNTIL_PREFIX,
                        FRONT_UNTIL_PREFIX,
                        "drag:",
                        "front:",
                        "activate:",
                        "prepare:",
                        "move-peer:",
                        "pointer:",
                    )
                )
            ) and time.monotonic() - last_switch > 0.25:
                driver_trace(focus, "driver.request", request.split(":", 1)[0])
                if request == "peer-deferred":
                    # The readiness fixture holds a claim long enough to expose
                    # a receiver that replaces its focus request with a gesture.
                    time.sleep(0.1)
                    request = "peer"

                def front(title):
                    if backend == "x11":
                        windows = xdo("search", "--onlyvisible", "--name", title).splitlines()
                        if not windows:
                            raise RuntimeError("Owned drag window missing: " + title)
                        xdo("windowraise", windows[0])
                        xdo("windowfocus", "--sync", windows[0])
                    else:
                        xdo("key", "--clearmodifiers", SWITCH_WINDOW_KEY)
                    time.sleep(0.3)

                if request == "geometry":
                    acknowledge(focus)
                    # Read only the two owned drag fixture surfaces. Wayland
                    # clients cannot query their global compositor positions.
                    mapping_deadline = time.monotonic() + 3
                    while True:
                        scene = subprocess.run(
                            ["weston-debug", "scene-graph"],
                            env=environment,
                            capture_output=True,
                            text=True,
                            timeout=2,
                            check=True,
                        ).stdout
                        positions = {
                            name: (int(x), int(y))
                            for name, x, y in re.findall(
                                r"top-level window 'wxnotepad stream drag (main|peer)'[^\n]*\n\s*position: \((-?\d+), (-?\d+)\)",
                                scene,
                            )
                        }
                        if set(positions) == {"main", "peer"}:
                            break
                        if time.monotonic() > mapping_deadline:
                            raise RuntimeError("Private compositor must map both owned drag surfaces")
                        time.sleep(0.05)
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
                    time.sleep(0.15)
                    xdo("mousemove", str(x2 + ox), str(y2 + oy))
                    time.sleep(0.2)
                    xdo("mouseup", "1")
                    xdo("keyup", "Super_L")
                    acknowledge(focus)
                elif request.startswith(("activate:", "prepare:")):
                    kind, process, surface, expiry = request.split(":")
                    deadline = int(expiry) / 1_000_000
                    last_scene = matched = None
                    queries = 0
                    timed_out = acknowledged = False
                    while backend == "wayland" and time.monotonic() < deadline:
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            break
                        try:
                            queries += 1
                            scene = subprocess.run(
                                ["weston-debug", "scene-graph"],
                                env=environment,
                                capture_output=True,
                                text=True,
                                timeout=min(2, remaining),
                                check=True,
                            ).stdout
                        except subprocess.TimeoutExpired:
                            timed_out = True
                            break
                        last_scene = scene
                        matched = input_ready_toplevel(scene, int(process), int(surface))
                        if matched:
                            if time.monotonic() < deadline:
                                if kind == "activate":
                                    xdo("key", "--clearmodifiers", SWITCH_WINDOW_KEY)
                                acknowledged = acknowledge(focus, int(expiry))
                            break
                        time.sleep(min(0.03, max(0, deadline - time.monotonic())))
                    if kind == "activate" and not acknowledged:
                        retain_activation_failure(process, surface, expiry, last_scene, matched, queries, timed_out)
                elif request.startswith("pointer:"):
                    _, process, surface, x, y, expiry = request.split(":")
                    assert backend == "wayland", "Owned surface pointer input requires the private Wayland driver"
                    remaining = int(expiry) / 1_000_000 - time.monotonic()
                    if remaining <= 0:
                        acknowledge(focus, int(expiry))
                        continue
                    scene = subprocess.run(
                        ["weston-debug", "scene-graph"],
                        env=environment,
                        capture_output=True,
                        text=True,
                        timeout=min(2, remaining),
                        check=True,
                    ).stdout
                    origin = mapped_toplevel_position(scene, int(process), int(surface))
                    if origin is None and os.environ.get("WXNOTEPAD_DISPLAY_LOG_DIR"):
                        observation = Path(os.environ["WXNOTEPAD_DISPLAY_LOG_DIR"]) / "pointer-unmapped.scene.txt"
                        observation.parent.mkdir(parents=True, exist_ok=True)
                        observation.write_text(request + "\n" + scene, encoding="utf-8")
                    assert origin is not None, "Pointer target must be the exact owned mapped surface"
                    geometry = xdo("getwindowgeometry", "--shell", weston_window)
                    values = dict(line.split("=", 1) for line in geometry.splitlines())
                    if not acknowledge(focus, int(expiry)):
                        continue
                    xdo(
                        "mousemove",
                        str(origin[0] + int(x) + int(values["X"])),
                        str(origin[1] + int(y) + int(values["Y"])),
                    )
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
                                raise RuntimeError(
                                    f"Native drag gesture={gesture} from={x1},{y1} to={x2},{y2} "
                                    f"must observe native phase {phase}"
                                )
                            time.sleep(0.01)

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
                        if key:
                            xdo("keydown", key)
                        if switch:
                            front("wxnotepad stream drag " + switch)
                        pending.write_text(f"{gesture}:target", encoding="utf-8")
                        pending.replace(phase)
                        move(x2, y2)
                        wait_drag_state(".drag-motion", f"target:{action}")
                        if action != "0":
                            wait_drag_state(".drag-action", f"target:{action}")
                        time.sleep(int(pause) / 1000)
                        if int(cancel):
                            xdo("key", "Escape")
                            time.sleep(0.3)
                            if backend == "wayland":
                                # Nested Weston withdraws keyboard focus throughout
                                # DnD. Cancel through a native rejected drop instead.
                                move(1500, 950)
                                time.sleep(0.2)
                    finally:
                        xdo("mouseup", "1")
                        if key:
                            xdo("keyup", key)
                        pending.write_text(f"{gesture}:released", encoding="utf-8")
                        pending.replace(phase)
                    wait_drag_state(".drag-ended", "end")
                    acknowledge(focus)
                elif request.startswith(("focus:", FOCUS_UNTIL_PREFIX, FRONT_UNTIL_PREFIX)):
                    if request.startswith((FOCUS_UNTIL_PREFIX, FRONT_UNTIL_PREFIX)):
                        _, expiry, title = request.split(":", 2)
                        deadline_us = int(expiry)
                    else:
                        deadline_us, title = None, request[6:]
                    if not focus_owned_window(
                        focus, title, deadline_us, raise_window=request.startswith(FRONT_UNTIL_PREFIX)
                    ):
                        time.sleep(0.03)
                        continue
                elif request.startswith("tree-drag:"):
                    x1, y1, x2, y2 = map(int, request[10:].split(","))
                    if backend == "wayland":
                        geometry = xdo("getwindowgeometry", "--shell", weston_window)
                        values = dict(line.split("=", 1) for line in geometry.splitlines())
                        x1 += int(values["X"])
                        x2 += int(values["X"])
                        y1 += int(values["Y"])
                        y2 += int(values["Y"])
                    xdo("mousemove", str(x1), str(y1))
                    xdo("mousedown", "1")
                    try:
                        motion_steps = 8
                        for step in range(1, motion_steps + 1):
                            xdo(
                                "mousemove",
                                str(x1 + (x2 - x1) * step // motion_steps),
                                str(y1 + (y2 - y1) * step // motion_steps),
                            )
                            time.sleep(0.03)
                    finally:
                        xdo("mouseup", "1")
                    acknowledge(focus)
                elif request.startswith("click:"):
                    x, y, button = map(int, request[6:].split(","))
                    if backend == "wayland":
                        geometry = xdo("getwindowgeometry", "--shell", weston_window)
                        values = dict(line.split("=", 1) for line in geometry.splitlines())
                        x += int(values["X"])
                        y += int(values["Y"])
                    acknowledge(focus)
                    xdo("mousemove", str(x), str(y))
                    xdo("click", str(button))
                elif request.startswith(("keydown:", "keyup:")):
                    phase, key = request.split(":", 1)
                    acknowledge(focus)
                    driver_trace(focus, "driver.inject-begin", phase)
                    xdo(phase, key)
                    driver_trace(focus, "driver.inject-complete", phase)
                elif request.startswith(("key:", KEY_DELAY_PREFIX)):
                    acknowledge(focus)
                    driver_trace(focus, "driver.inject-begin", "key-press-release")
                    if request.startswith(KEY_DELAY_PREFIX):
                        _, delay, keys = request.split(":", 2)
                        xdo("key", "--clearmodifiers", "--delay", delay, keys)
                    else:
                        xdo("key", "--clearmodifiers", request[4:])
                    driver_trace(focus, "driver.inject-complete", "key-press-release")
                elif request == "gesture":
                    # Native events can make the receiver publish its next
                    # request before xdotool exits. Claim this one first.
                    acknowledge(focus)
                    xdo("key", "--clearmodifiers", "Shift_L")
                elif backend == "x11":
                    windows = xdo(
                        "search", "--onlyvisible", "--name", "wxnotepad clipboard focus: " + request
                    ).splitlines()
                    if not windows:
                        time.sleep(0.03)
                        continue
                    window = windows[0]
                    acknowledge(focus)
                    xdo("windowfocus", "--sync", window)
                else:
                    # Claim the focus request before native events can publish
                    # the next gesture, as for key and gesture requests above.
                    acknowledge(focus)
                    xdo("windowfocus", "--sync", weston_window)
                    xdo("key", "--clearmodifiers", SWITCH_WINDOW_KEY)
                last_switch = time.monotonic()
            time.sleep(0.03)
        result = test.returncode
    finally:
        retain_focus_failure(runtime / "focus")
        stop(test)
        stop(weston)
        log.close()
        if os.environ.get("WXNOTEPAD_DISPLAY_LOG_DIR"):
            artifacts = Path(os.environ["WXNOTEPAD_DISPLAY_LOG_DIR"])
            artifacts.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(runtime / WESTON_LOG, artifacts / f"{backend}-{os.getpid()}-weston.log")
        if weston is not None:
            print((runtime / WESTON_LOG).read_text(encoding="utf-8")[-1500:], flush=True)
    raise SystemExit(result)
