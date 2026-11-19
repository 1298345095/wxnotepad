"""Capture native color CallTips and replacements from the pinned Notepad4 app."""

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import shutil
import time

from original_host import CALLBACK, Original, user


MODES = ("Off", "RGBA", "ARGB", "BGRA", "ABGR")
REFERENCE_REVISION = "89b8b36e0ee2c23f73be922315f17261ccfbbe1a"
SOURCE = {
    "short": (b"pre #aB3 and", (4, 5, 7, 8)),
    "short-alpha": (b"pre #1Bd4 and", (5, 8)),
    "short-alpha-zero": (b"pre #0BD0 and", (6,)),
    "six": (b"pre #12A5EF and", (5, 10)),
    "eight": (b"pre #12a5ef74 and", (5, 12)),
    "zero-alpha": (b"pre #00a5ef00 and", (7,)),
    "0x-six": (b"pre 0x12A5Ef and", (4, 5, 6, 11, 12)),
    "0X-eight": (b"pre 0X12a5EF74 and", (6,)),
    "0x-short": (b"pre 0xAB3 and", (6,)),
    "bad-prefix": (b"pre 1x12A5Ef and", (7,)),
    "too-long": (b"pre #0123456789AB and", (5, 9, 15)),
    "bad-length": (b"pre #12a5e and", (6,)),
    "neighbor-nonhex": (b"pre q#123z and", (7, 8)),
    "eof": (b"pre #123", (7,)),
}
DWELL_TIMER = 6


def configure_win32():
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.GetDpiForWindow.argtypes = [w.HWND]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.GetDlgItem.argtypes = [w.HWND, c.c_int]
    user.GetDlgItem.restype = w.HWND
    user.SetThreadDpiAwarenessContext.argtypes = [w.HANDLE]
    user.SetThreadDpiAwarenessContext.restype = w.HANDLE


def visible_window(desktop, app, class_name):
    matches = []

    @CALLBACK
    def window(hwnd, _):
        pid = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, c.byref(pid))
        if pid.value == app.process.pid and user.IsWindowVisible(hwnd):
            name = c.create_unicode_buffer(64)
            user.GetClassNameW(hwnd, name, len(name))
            if name.value == class_name:
                matches.append(hwnd)
        return True

    user.EnumDesktopWindows(desktop, window, 0)
    assert len(matches) <= 1, (class_name, matches)
    return matches[0] if matches else None


def wait_until(condition, timeout=3):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = condition()
        if result:
            return result
        time.sleep(.01)
    return None


def move(app, position):
    x = app.send(2164, 0, position) + 1  # SCI_POINTXFROMPOSITION
    y = app.send(2165, 0, position) + app.send(2279, 0) // 2  # SCI_TEXTHEIGHT
    assert x >= 0 and y >= 0
    observed_position = app.send(2023, x, y)  # SCI_POSITIONFROMPOINTCLOSE
    enabled = app.send(2265) != 10000000
    assert user.PostMessageW(app.editor, 0x0200, 0, x | (y << 16))
    if enabled:
        assert user.PostMessageW(app.editor, 0x0113, DWELL_TIMER, 0)
    return observed_position


def hover(app, desktop, position):
    app.send(2201)  # cancel any earlier popup; leave resets Scintilla's dwell timer
    app.send(0x02A3)  # WM_MOUSELEAVE
    observed_position = move(app, position)
    start = time.monotonic()
    popup = wait_until(lambda: visible_window(desktop, app, "CallTip"), timeout=.5)
    result = {"hover": position, "point_position": observed_position, "active": bool(app.send(2202))}
    assert result["active"] == bool(popup)
    if popup:
        rect = w.RECT()
        assert user.GetWindowRect(popup, c.byref(rect))
        result.update(elapsed_ms=round((time.monotonic() - start) * 1000),
                      caret_at_show=app.send(2203), bounds=[rect.left, rect.top, rect.right, rect.bottom])
    return result, popup


def choose(app, desktop, popup, choice):
    assert user.PostMessageW(popup, 0x0201, 1, 1 | (1 << 16))  # WM_LBUTTONDOWN
    dialog = wait_until(lambda: visible_window(desktop, app, "#32770"))
    assert dialog, "native color chooser appeared"
    fields = (706, 707, 708)  # Win32 COLOR_RED, COLOR_GREEN, COLOR_BLUE
    assert all(user.GetDlgItem(dialog, field) for field in fields)
    initial_rgb = []
    for field in fields:
        text = c.create_unicode_buffer(4)
        assert user.SendMessageW(user.GetDlgItem(dialog, field), 0x000D, len(text), c.addressof(text))
        initial_rgb.append(int(text.value))
    if isinstance(choice, tuple):
        for field, channel in zip(fields, choice):
            text = c.create_unicode_buffer(str(channel))
            assert user.SendMessageW(user.GetDlgItem(dialog, field), 0x000C, 0, c.addressof(text))
    user.SendMessageW(dialog, 0x0111, 2 if choice == "cancel" else 1, 0)
    assert user.WaitForInputIdle(app.process.handle, 3000) == 0
    assert not visible_window(desktop, app, "#32770"), "native color chooser dismissed"
    return initial_rgb, app.state()


def capture(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    executable = output / "Notepad4.exe"
    shutil.copy2(args.original, executable)
    (output / "Notepad4.ini").write_text("[Settings]\nSaveSettings=0\n[Settings2]\nUILanguage=1033\n", encoding="utf-16")
    configure_win32()
    assert user.SetThreadDpiAwarenessContext(w.HANDLE(-4))
    name = f"wxnotepad-calltip-reference-{os.getpid()}"
    desktop = user.CreateDesktopW(name, None, None, 0, 0x1ff, None)
    assert desktop
    app = None
    try:
        app = Original(None, executable, desktop, name)
        results = {"revision": REFERENCE_REVISION,
                   "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                   "dpi": user.GetDpiForWindow(app.editor), "technology": app.send(2631),
                   "hover_driver": "owned Win32 motion and dwell timer messages", "modes": {}}
        for offset, mode in enumerate(MODES):
            app.command(40055 + offset)  # IDM_VIEW_SHOWCALLTIP_OFF plus the mode ordinal
            data = {"dwell_ms": app.send(2265), "observations": [], "edits": []}
            for sample, (source, positions) in SOURCE.items():
                for position in positions:
                    app.reset(source, 0, 0)
                    record, popup = hover(app, desktop, position)
                    record.update(sample=sample, source=source.decode("ascii"))
                    if popup and position == positions[0]:
                        record["rgb"], _ = choose(app, desktop, popup, "cancel")
                    data["observations"].append(record)
            if mode != "Off":
                for sample in ("short", "short-alpha", "short-alpha-zero", "zero-alpha", "0X-eight"):
                    source, positions = SOURCE[sample]
                    choices = ("cancel", "same", (205, 171, 239)) if sample == "short" else ((205, 171, 239),)
                    for choice in choices:
                        app.reset(source, 1, 3)
                        before = app.state()
                        _, popup = hover(app, desktop, positions[0] + (sample == "short"))
                        assert popup, (mode, sample, choice)
                        initial, after = choose(app, desktop, popup, choice)
                        app.send(2176)  # SCI_UNDO
                        undone = app.state()
                        app.send(2011)  # SCI_REDO
                        data["edits"].append({"sample": sample, "choice": choice, "rgb": initial,
                                              "before": before, "after": after, "undo": undone, "redo": app.state()})
            results["modes"][mode] = data
            (output / "original.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
            print(f"Captured {mode}: {len(data['observations'])} hovers, {len(data['edits'])} chooser interactions",
                  flush=True)
        results["lifecycle"] = lifecycle(app, desktop)
        (output / "original.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    finally:
        if app:
            dialog = visible_window(desktop, app, "#32770")
            if dialog:
                user.PostMessageW(dialog, 0x0111, 2, 0)
            app.close()
        user.CloseDesktop(desktop)


def lifecycle(app, desktop):
    app.command(40056)
    source, _ = SOURCE["short"]
    app.reset(source, 0, 0)
    _, popup = hover(app, desktop, 5)
    assert popup
    app.send(0x02A3)
    time.sleep(.35)
    after_leave = bool(app.send(2202))
    move(app, 8)
    time.sleep(.35)
    after_dwell_outside = bool(app.send(2202))
    _, popup = hover(app, desktop, 5)
    assert popup
    app.command(40550)
    after_escape = bool(app.send(2202))
    _, popup = hover(app, desktop, 5)
    assert popup
    app.send(0x0102, ord("q"))
    after_typing = bool(app.send(2202))
    app.reset(source, 0, 0)
    app.send(2100, 0, app.put(b"alpha alpine\0"))
    assert app.send(2102), "native autocomplete is active before hover"
    _, completion_popup = hover(app, desktop, 5)
    after_completion_hover = {"calltip": bool(completion_popup), "completion": bool(app.send(2102))}
    app.reset(source, 0, 0)
    _, popup = hover(app, desktop, 5)
    assert popup
    app.command(40055)
    return {"after_leave": after_leave, "after_dwell_outside": after_dwell_outside,
            "after_escape": after_escape, "after_typing": after_typing,
            "after_completion_hover": after_completion_hover,
            "after_off_command": bool(app.send(2202))}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    capture(parser.parse_args())
