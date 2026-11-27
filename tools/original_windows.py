"""Find the pinned original editor's owned Windows frame and Scintilla controls."""

import ctypes as c
import time
from ctypes import wintypes as w


def find_frames(user, process, callback, timeout=10):
    windows = []

    def window(hwnd, _):
        pid = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, c.byref(pid))
        if pid.value == process.pid:
            name = c.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, 128)
            if name.value == "Notepad4":
                windows.append(hwnd)
        return True

    deadline = time.monotonic() + timeout
    while not windows and time.monotonic() < deadline:
        user.EnumWindows(callback(window), 0)
        time.sleep(0.05)
    return windows


def find_editors(user, frame, callback):
    children = []

    def child(hwnd, _):
        name = c.create_unicode_buffer(128)
        user.GetClassNameW(hwnd, name, 128)
        if name.value == "Scintilla":
            children.append(hwnd)
        return True

    user.EnumChildWindows(frame, callback(child), 0)
    return children
