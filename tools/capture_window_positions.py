"""Capture the pinned original's geometry, clipboard and INI in an owned copy."""

import argparse
import configparser
import ctypes as c
import hashlib
import json
import shutil
import subprocess
import threading
import time
from ctypes import wintypes as w
from pathlib import Path

from original_windows import find_frames

REVISION = "89b8b36e0ee2c23f73be922315f17261ccfbbe1a"
ORIGINAL_SHA256 = "1425c9a4b5ab2ea1fa3ba088f38dd987b763b7b8feff23dde6243f7855e86a78"


class Placement(c.Structure):
    _fields_ = [
        ("length", w.UINT),
        ("flags", w.UINT),
        ("show", w.UINT),
        ("min", w.POINT),
        ("max", w.POINT),
        ("normal", w.RECT),
    ]


class Monitor(c.Structure):
    _fields_ = [("size", w.DWORD), ("screen", w.RECT), ("work", w.RECT), ("flags", w.DWORD)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    digest = hashlib.sha256(args.original.read_bytes()).hexdigest()
    assert digest == ORIGINAL_SHA256, digest
    args.output.mkdir(parents=True, exist_ok=False)
    portable = args.output / "portable"
    portable.mkdir()
    executable = portable / "Notepad4.exe"
    shutil.copy2(args.original, executable)
    config = portable / "Notepad4.ini"
    config.write_text("[Settings]\nSaveSettings=0\n[Settings2]\nUILanguage=1033\n", encoding="utf-16")
    user, kernel = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True)
    user.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
    assert user.SetProcessDpiAwarenessContext(w.HANDLE(-4)), "capture process must use physical PMv2 geometry"
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    monitor_callback = c.WINFUNCTYPE(w.BOOL, w.HANDLE, w.HDC, c.POINTER(w.RECT), w.LPARAM)
    user.EnumDisplayMonitors.argtypes = [w.HDC, c.POINTER(w.RECT), monitor_callback, w.LPARAM]
    user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.SendMessageW.restype = c.c_ssize_t
    user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetDpiForWindow.argtypes = [w.HWND]
    user.GetWindowPlacement.argtypes = [w.HWND, c.POINTER(Placement)]
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.MonitorFromWindow.argtypes = [w.HWND, w.DWORD]
    user.MonitorFromWindow.restype = w.HANDLE
    user.GetMonitorInfoW.argtypes = [w.HANDLE, c.POINTER(Monitor)]
    user.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
    user.ShowWindow.argtypes = [w.HWND, c.c_int]
    user.IsZoomed.argtypes = [w.HWND]
    user.GetMenu.argtypes = [w.HWND]
    user.GetMenu.restype = w.HMENU
    user.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]
    user.OpenClipboard.argtypes = [w.HWND]
    user.GetClipboardData.argtypes = [w.UINT]
    user.GetClipboardData.restype = w.HANDLE
    kernel.GlobalLock.argtypes = [w.HANDLE]
    kernel.GlobalLock.restype = w.LPVOID
    kernel.GlobalUnlock.argtypes = [w.HANDLE]
    process = subprocess.Popen([str(executable), "/n", "/pos", "100,120,640,480,0"])
    records = []

    def rect(value):
        return [value.left, value.top, value.right - value.left, value.bottom - value.top]

    def state(label):
        placement = Placement(c.sizeof(Placement))
        outer = w.RECT()
        assert user.GetWindowPlacement(frame, c.byref(placement))
        assert user.GetWindowRect(frame, c.byref(outer))
        current_monitor = Monitor(c.sizeof(Monitor))
        assert user.GetMonitorInfoW(user.MonitorFromWindow(frame, 2), c.byref(current_monitor))
        stored = configparser.ConfigParser(interpolation=None)
        stored.read(config, encoding="utf-16")
        user.SendMessageW(frame, 0x116, user.GetMenu(frame), 0)  # Original native menu preparation.
        records.append(
            dict(
                case=label,
                outer=rect(outer),
                normal=rect(placement.normal),
                maximized=bool(user.IsZoomed(frame) or placement.flags & 2),
                clear_state=user.GetMenuState(user.GetMenu(frame), 40045, 0),
                dpi=user.GetDpiForWindow(frame),
                monitor=rect(current_monitor.screen),
                work=rect(current_monitor.work),
                ini={name: dict(stored[name]) for name in stored.sections()},
            )
        )

    def command(identity):
        if identity == 40581:
            assert user.OpenClipboard(frame)
            try:
                assert user.EmptyClipboard()
            finally:
                user.CloseClipboard()
        pending = threading.Thread(target=lambda: user.SendMessageW(frame, 0x111, identity, 0), daemon=True)
        pending.start()
        deadline = time.monotonic() + 4
        while pending.is_alive() and time.monotonic() < deadline:
            pending.join(0.05)

            def dismiss(hwnd, _):
                owner = w.DWORD()
                user.GetWindowThreadProcessId(hwnd, c.byref(owner))
                name = c.create_unicode_buffer(128)
                user.GetClassNameW(hwnd, name, len(name))
                if owner.value == process.pid and name.value == "#32770":
                    title = c.create_unicode_buffer(256)
                    user.GetWindowTextW(hwnd, title, len(title))
                    print(f"Dismissing owned command {identity} information dialog: {title.value}", flush=True)
                    user.PostMessageW(hwnd, 0x10, 0, 0)  # Owned Save Settings Now information dialog.
                return True

            user.EnumWindows(callback(dismiss), 0)
        assert not pending.is_alive(), f"original command {identity} did not finish"
        time.sleep(0.1)

    def clipboard():
        assert user.OpenClipboard(frame)
        try:
            data = user.GetClipboardData(13)
            pointer = kernel.GlobalLock(data)
            assert pointer
            value = c.wstring_at(pointer)
            kernel.GlobalUnlock(data)
            return value
        finally:
            user.CloseClipboard()

    try:
        frames = find_frames(user, process, callback)
        assert len(frames) == 1
        frame = frames[0]
        time.sleep(0.3)
        monitor = Monitor(c.sizeof(Monitor))
        assert user.GetMonitorInfoW(user.MonitorFromWindow(frame, 2), c.byref(monitor))
        state("startup-pos")
        command(40581)
        copied = clipboard()
        command(40472)
        state("sticky-enabled-save-off")
        user.SetWindowPos(frame, None, 180, 210, 700, 510, 0x14)
        command(40444)
        state("sticky-moved-explicit-save")
        command(40472)
        command(40444)
        state("ordinary-save-before-fullscreen")
        command(40468)
        command(40444)
        state("fullscreen-save")
        command(40581)
        fullscreen_copied = clipboard()
        command(40468)
        user.ShowWindow(frame, 6)
        command(40444)
        state("minimized-save")
        user.ShowWindow(frame, 9)
        command(40045)
        state("history-cleared")
        command(40582)
        state("default-right")
        command(40582)
        state("default-left")
        user.ShowWindow(frame, 3)
        time.sleep(0.2)
        command(40582)
        state("default-from-maximized")
        command(40444)
        state("explicit-save")
        user.SendMessageW(frame, 0x10, 0, 0)
        process.wait(timeout=10)
        process = subprocess.Popen([str(executable), "/n", *copied.split()])
        frames = find_frames(user, process, callback)
        assert len(frames) == 1
        frame = frames[0]
        time.sleep(0.2)
        state("clipboard-relaunch")
        command(40443)  # Enable ordinary exit saving, then observe Clear -> exit -> restart.
        user.SetWindowPos(frame, None, 180, 210, 700, 510, 0x14)
        command(40444)
        command(40045)
        state("clear-with-exit-saving")
        user.SendMessageW(frame, 0x10, 0, 0)
        process.wait(timeout=10)
        process = subprocess.Popen([str(executable), "/n"])
        frames = find_frames(user, process, callback)
        assert len(frames) == 1
        frame = frames[0]
        time.sleep(0.2)
        state("restart-after-clear-with-exit-saving")
        monitors = []

        def collect_monitor(handle, _dc, _rect, _data):
            info = Monitor(c.sizeof(Monitor))
            assert user.GetMonitorInfoW(handle, c.byref(info))
            monitors.append(info)
            return True

        assert user.EnumDisplayMonitors(None, None, monitor_callback(collect_monitor), 0)
        for index, info in enumerate(monitors):
            if info.flags & 1:  # MONITORINFOF_PRIMARY.
                continue
            user.SetWindowPos(frame, None, info.work.left + 40, info.work.top + 40, 640, 480, 0x14)
            time.sleep(0.2)
            command(40582)
            state(f"non-primary-default-{index}")
            command(40581)
            exported = clipboard()
            rectangle = records[-1]["normal"]
            assert exported == "/pos " + ",".join(map(str, (*rectangle, 0)))
            user.SendMessageW(frame, 0x10, 0, 0)
            process.wait(timeout=10)
            process = subprocess.Popen([str(executable), "/n", *exported.split()])
            frames = find_frames(user, process, callback)
            assert len(frames) == 1
            frame = frames[0]
            time.sleep(0.2)
            state(f"non-primary-clipboard-relaunch-{index}")
            assert records[-1]["normal"] == rectangle
        report = dict(
            revision=REVISION,
            executable_sha256=digest,
            copied=copied,
            fullscreen_copied=fullscreen_copied,
            dpi=user.GetDpiForWindow(frame),
            monitors=[
                dict(screen=rect(info.screen), work=rect(info.work), primary=bool(info.flags & 1)) for info in monitors
            ],
            monitor=rect(monitor.screen),
            work=rect(monitor.work),
            records=records,
        )
        (args.output / "reference.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        user.SendMessageW(frame, 0x10, 0, 0)
        process.wait(timeout=10)
        assert copied == "/pos 100,120,640,480,0", copied
        relaunched = next(record for record in records if record["case"] == "clipboard-relaunch")
        assert relaunched["normal"] == [100, 120, 640, 480], relaunched
        print("PASS: owned pinned original position, sticky, clear, defaults, clipboard and relaunch")
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)


if __name__ == "__main__":
    main()
