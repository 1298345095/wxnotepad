"""Capture original topmost preference/override/manual-toggle and real owned stacking."""

import argparse
import ctypes as c
import hashlib
import json
import shutil
import threading
import time
from ctypes import wintypes as w
from pathlib import Path
from types import SimpleNamespace

from original_host import CALLBACK, Original, kernel, user

# Pinned Notepad4/Scintilla and native platform protocol values.
DESKTOP_READOBJECTS = 1
GW_HWNDPREV = 3
PM_REMOVE = 1
UOI_NAME = 2
WM_CLOSE = 0x10
WS_VISIBLE_OVERLAPPEDWINDOW = 0x10CF0000

ALWAYS_ON_TOP = 40425
SAVE_SETTINGS_NOW = 40444
GWL_EXSTYLE = -20
WS_EX_TOPMOST = 8
SWP_NOMOVE = 2
SWP_NOSIZE = 1
SWP_NOACTIVATE = 16
SWP_SHOWWINDOW = 64

WindowProcedure = c.WINFUNCTYPE(c.c_ssize_t, w.HWND, w.UINT, w.WPARAM, w.LPARAM)


class WindowClass(c.Structure):
    _fields_ = [
        ("style", w.UINT),
        ("procedure", WindowProcedure),
        ("class_extra", c.c_int),
        ("window_extra", c.c_int),
        ("instance", w.HINSTANCE),
        ("icon", w.HICON),
        ("cursor", w.HANDLE),
        ("background", w.HBRUSH),
        ("menu", w.LPCWSTR),
        ("name", w.LPCWSTR),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--current-desktop",
        action="store_true",
        required=True,
        help="Use the granted active input desktop for owned peer activation",
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    portable = args.output / "portable"
    portable.mkdir()
    executable = portable / "Notepad4.exe"
    shutil.copy2(args.executable, executable)
    ini = portable / "Notepad4.ini"
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.GetWindowLongPtrW.argtypes = [w.HWND, c.c_int]
    user.GetWindowLongPtrW.restype = c.c_ssize_t
    user.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
    user.GetWindow.argtypes = [w.HWND, w.UINT]
    user.GetWindow.restype = w.HWND
    user.GetThreadDesktop.argtypes = [w.DWORD]
    user.GetThreadDesktop.restype = w.HANDLE
    user.CreateWindowExW.argtypes = [
        w.DWORD,
        w.LPCWSTR,
        w.LPCWSTR,
        w.DWORD,
        c.c_int,
        c.c_int,
        c.c_int,
        c.c_int,
        w.HWND,
        w.HMENU,
        w.HINSTANCE,
        c.c_void_p,
    ]
    user.CreateWindowExW.restype = w.HWND
    user.DestroyWindow.argtypes = [w.HWND]
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.WindowFromPoint.argtypes = [w.POINT]
    user.WindowFromPoint.restype = w.HWND
    user.GetForegroundWindow.restype = w.HWND
    user.IsWindowVisible.argtypes = [w.HWND]
    user.UpdateWindow.argtypes = [w.HWND]
    user.SetCursorPos.argtypes = [c.c_int, c.c_int]
    user.mouse_event.argtypes = [w.DWORD, w.DWORD, w.DWORD, w.DWORD, c.c_size_t]
    user.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
    assert user.SetProcessDpiAwarenessContext(w.HANDLE(-4)), "reference uses physical PMv2 native coordinates"
    user.PeekMessageW.argtypes = [c.POINTER(w.MSG), w.HWND, w.UINT, w.UINT, w.UINT]
    user.TranslateMessage.argtypes = [c.POINTER(w.MSG)]
    user.DispatchMessageW.argtypes = [c.POINTER(w.MSG)]
    user.DefWindowProcW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.DefWindowProcW.restype = c.c_ssize_t
    user.RegisterClassW.argtypes = [c.POINTER(WindowClass)]
    kernel.GetModuleHandleW.argtypes = [w.LPCWSTR]
    kernel.GetModuleHandleW.restype = w.HINSTANCE
    instance = kernel.GetModuleHandleW(None)
    procedure = WindowProcedure(
        lambda window, message, first, second: user.DefWindowProcW(window, message, first, second)
    )
    peer_class = WindowClass(
        procedure=procedure, instance=instance, background=w.HBRUSH(6), name="WxNotepadOwnedTopmostPeer289"
    )
    assert user.RegisterClassW(c.byref(peer_class)), "owned normal peer uses a native top-level window class"
    desktop = user.GetThreadDesktop(c.windll.kernel32.GetCurrentThreadId())
    name = None
    user.OpenInputDesktop.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    user.OpenInputDesktop.restype = w.HANDLE
    user.GetUserObjectInformationW.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)]
    input_desktop = user.OpenInputDesktop(0, False, DESKTOP_READOBJECTS)
    assert input_desktop, "reference requires the granted input desktop"
    desktop_names = []
    for handle in (desktop, input_desktop):
        value = c.create_unicode_buffer(128)
        needed = w.DWORD()
        assert user.GetUserObjectInformationW(handle, UOI_NAME, value, c.sizeof(value), c.byref(needed))
        desktop_names.append(value.value)
    assert user.CloseDesktop(input_desktop)
    assert desktop_names == ["Default", "Default"], desktop_names
    rows = []

    def pump():
        message = w.MSG()
        while user.PeekMessageW(c.byref(message), None, 0, 0, PM_REMOVE):
            user.TranslateMessage(c.byref(message))
            user.DispatchMessageW(c.byref(message))

    def topmost(app):
        return bool(user.GetWindowLongPtrW(app.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)

    def above(app, peer):
        window = user.GetWindow(peer.hwnd, GW_HWNDPREV)  # GW_HWNDPREV: preceding native Z-order window.
        while window:
            if window == app.hwnd:
                return True
            window = user.GetWindow(window, GW_HWNDPREV)
        return False

    def save_now(app):
        pending = threading.Thread(target=lambda: app.command(SAVE_SETTINGS_NOW), daemon=True)
        pending.start()
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            pump()
            dialogs = []

            def collect(window, unused):
                process = w.DWORD()
                user.GetWindowThreadProcessId(window, c.byref(process))
                name = c.create_unicode_buffer(80)
                user.GetClassNameW(window, name, len(name))
                if process.value == app.process.pid and name.value == "#32770":
                    dialogs.append(window)
                return True

            user.EnumDesktopWindows(desktop, CALLBACK(collect), 0)
            if dialogs:
                assert len(dialogs) == 1
                text = []

                def child(window, unused):
                    value = c.create_unicode_buffer(1024)
                    user.GetWindowTextW(window, value, len(value))
                    text.append(value.value)
                    return True

                user.EnumChildWindows(dialogs[0], CALLBACK(child), 0)
                if "The current program settings have been saved." not in text:
                    time.sleep(0.01)
                    continue
                assert user.PostMessageW(dialogs[0], WM_CLOSE, 0, 0)
                while pending.is_alive() and time.monotonic() < deadline:
                    pump()
                    pending.join(0.01)
                assert not pending.is_alive(), "original Save Settings Now completes after owned confirmation"
                return
            time.sleep(0.01)
        raise AssertionError("owned original Save Settings Now confirmation must appear within three seconds")

    def check_stacking(app, peer, expected):
        deadline = time.monotonic() + 3
        assert user.SetWindowPos(peer.hwnd, None, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW)
        assert user.UpdateWindow(peer.hwnd)
        pump()
        assert user.IsWindowVisible(peer.hwnd), "owned peer is actually visible before its native input gesture"
        rectangle = w.RECT()
        assert user.GetWindowRect(peer.hwnd, c.byref(rectangle))
        point = w.POINT(rectangle.left + 100, rectangle.top + 20)
        assert user.WindowFromPoint(point) == peer.hwnd, "native click must hit only the owned peer caption"
        assert user.SetCursorPos(point.x, point.y)
        user.mouse_event(2, 0, 0, 0, 0)  # MOUSEEVENTF_LEFTDOWN.
        user.mouse_event(4, 0, 0, 0, 0)  # MOUSEEVENTF_LEFTUP.
        while time.monotonic() < deadline:
            pump()
            if user.GetForegroundWindow() == peer.hwnd and above(app, peer) == expected:
                return
            time.sleep(0.01)
        raise AssertionError(
            f"owned normal peer must prove original stacking: expected={expected} "
            f"topmost={topmost(app)} peerTopmost={topmost(peer)} appAbove={above(app, peer)}"
        )

    try:
        for preference in (False, True):
            for switch, forced in ((None, None), ("/o", True), ("/o0", False), ("/o-", False), ("/oo", False)):
                ini.write_text(
                    f"[Settings]\nSaveSettings=0\nAlwaysOnTop={int(preference)}\nWordWrap=0\n"
                    "[Settings2]\nUILanguage=1033\n",
                    encoding="utf-16",
                )
                app = Original(None, executable, desktop, name, arguments=(() if switch is None else (switch,)))
                peer = None
                try:
                    expected = preference if forced is None else forced
                    print(f"original preference={preference} switch={switch} expected={expected}", flush=True)
                    assert topmost(app) == expected
                    save_now(app)
                    saved_before = ini.read_text(encoding="utf-16")
                    assert ("AlwaysOnTop=1" in saved_before) == preference, saved_before
                    app.close()
                    app = Original(None, executable, desktop, name, arguments=(() if switch is None else (switch,)))
                    assert topmost(app) == expected
                    app.reset(b"alpha beta\r\n", 8, 2)
                    before = app.state()
                    bounds = w.RECT()
                    assert user.GetWindowRect(app.hwnd, c.byref(bounds))
                    screen_width, screen_height = user.GetSystemMetrics(0), user.GetSystemMetrics(1)
                    candidates = (
                        (0, 0),
                        (screen_width - 400, 0),
                        (0, screen_height - 300),
                        (screen_width - 400, screen_height - 300),
                    )
                    position = next(
                        (x, y)
                        for x, y in candidates
                        if not (bounds.left <= x + 100 < bounds.right and bounds.top <= y + 20 < bounds.bottom)
                    )
                    peer = SimpleNamespace(
                        hwnd=user.CreateWindowExW(
                            0,
                            peer_class.name,
                            "Owned original topmost peer",
                            WS_VISIBLE_OVERLAPPEDWINDOW,
                            *position,
                            400,
                            300,
                            None,
                            None,
                            None,
                            None,
                        )
                    )
                    assert peer.hwnd
                    check_stacking(app, peer, expected)
                    app.command(ALWAYS_ON_TOP)
                    assert topmost(app) != expected
                    assert app.state() == before
                    check_stacking(app, peer, not expected)
                    print(
                        f"after toggle app={app.hwnd} peer={peer.hwnd} appTopmost={topmost(app)} "
                        f"peerTopmost={topmost(peer)} appAbove={above(app, peer)}",
                        flush=True,
                    )
                    save_now(app)
                    saved_after = ini.read_text(encoding="utf-16")
                    assert ("AlwaysOnTop=1" in saved_after) != expected, saved_after
                    rows.append(
                        {
                            "preference": preference,
                            "switch": switch,
                            "initial_topmost": expected,
                            "after_manual_toggle": not expected,
                            "saved_before_toggle": saved_before,
                            "saved_after_toggle": saved_after,
                            "document": before,
                        }
                    )
                finally:
                    if peer:
                        assert user.DestroyWindow(peer.hwnd)
                    app.close()
    finally:
        pump()
    (args.output / "always_on_top_reference.json").write_text(
        json.dumps(
            {
                "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
                "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                "rows": rows,
            },
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print("PASS: independent original preference/override matrix, manual clearing and real owned stacking")


if __name__ == "__main__":
    main()
