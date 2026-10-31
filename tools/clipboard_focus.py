"""Run GUI checks while granting native foreground to owned clipboard fixtures.

Windows/WSLg: run this inside desktop_run.py, followed by a command such as
ctest or wsl.exe ... ctest. Uses no keyboard or mouse injection.
"""
import ctypes as c
from ctypes import wintypes as w
import subprocess
import re
import sys
import time

user = c.WinDLL("user32", use_last_error=True)
kernel = c.WinDLL("kernel32", use_last_error=True)
user.GetForegroundWindow.restype = w.HWND
user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
user.SetForegroundWindow.argtypes = [w.HWND]
user.BringWindowToTop.argtypes = [w.HWND]
user.AttachThreadInput.argtypes = [w.DWORD, w.DWORD, w.BOOL]
callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
grants = 0
seen = set()
inspect_only = len(sys.argv) > 1 and sys.argv[1] == "--inspect"
published = set()

def focus_requests():
    requests = []
    @callback_type
    def visit(hwnd, unused):
        if user.IsWindowVisible(hwnd):
            title = c.create_unicode_buffer(256)
            user.GetWindowTextW(hwnd, title, len(title))
            if inspect_only and ("wxnotepad clipboard" in title.value or title.value.startswith("Untitled - wxnotepad")) and (hwnd, title.value) not in published:
                published.add((hwnd, title.value))
                print(f"Published fixture candidate: {hwnd} {title.value}", flush=True)
            if re.search(r"wxnotepad clipboard focus: (editor|peer) \[\d+\]", title.value):
                requests.append(hwnd)
        return True
    user.EnumWindows(visit, 0)
    return requests

with subprocess.Popen(sys.argv[2:] if inspect_only else sys.argv[1:]) as process:
    while process.poll() is None:
        for window in focus_requests():
            seen.add(window)
            if inspect_only:
                continue
            if user.GetForegroundWindow() == window:
                continue
            thread = kernel.GetCurrentThreadId()
            foreground = user.GetWindowThreadProcessId(user.GetForegroundWindow(), None)
            attached = user.AttachThreadInput(thread, foreground, True)
            try:
                user.BringWindowToTop(window)
                user.SetForegroundWindow(window)
            finally:
                if attached:
                    user.AttachThreadInput(thread, foreground, False)
            if user.GetForegroundWindow() == window:
                grants += 1
        time.sleep(.03)
    print(f"Native clipboard fixture windows: {len(seen)}; foreground grants: {grants}", flush=True)
    sys.exit(process.returncode)
