"""Shared original-app session for character insertion and tag-closure captures."""

import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from original_windows import find_editors, find_frames


def prepare_host(executable, output):
    output.mkdir(parents=True, exist_ok=True)
    host = output / "original-portable"
    host.mkdir(exist_ok=True)
    shutil.copy2(executable, host / "Notepad4.exe")
    (host / "Notepad4.ini").write_text(
        "[Notepad4]\n[Settings]\nSaveSettings=0\nLoadASCIIasUTF8=0\nLoadANSIasUTF8=0\n"
        "NoEncodingTags=0\nSkipUnicodeDetection=0\n",
        encoding="utf-8",
    )
    return host


class Session:
    def __init__(self, executable, path):
        self.user = u = c.WinDLL("user32", use_last_error=True)
        self.kernel = k = c.WinDLL("kernel32", use_last_error=True)
        u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
        u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
        u.EnumChildWindows.argtypes = [w.HWND, c.c_void_p, c.c_ssize_t]
        u.SendMessageW.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
        u.SendMessageW.restype = c.c_ssize_t
        k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        k.OpenProcess.restype = w.HANDLE
        k.VirtualAllocEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD]
        k.VirtualAllocEx.restype = c.c_void_p
        k.WriteProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
        k.VirtualFreeEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD]
        k.CloseHandle.argtypes = [w.HANDLE]
        callback = c.WINFUNCTYPE(w.BOOL, w.HWND, c.c_ssize_t)
        self.process = subprocess.Popen([str(executable), "/ns", "/n", str(path)])
        windows = find_frames(u, self.process, callback, timeout=15)
        assert len(windows) == 1, windows
        self.frame = windows[0]
        editors = find_editors(u, self.frame, callback)
        assert len(editors) == 1
        self.editor = editors[0]
        self.handle = k.OpenProcess(0x38, False, self.process.pid)
        self.memory = k.VirtualAllocEx(self.handle, None, 65536, 0x3000, 4)
        assert self.memory

    def send(self, message, first=0, second=0):
        return self.user.SendMessageW(self.editor, message, first, second)

    def command(self, identifier):
        self.user.SendMessageW(self.frame, 0x111, identifier, 0)

    def state(self):
        text = bytes(self.send(2007, position) & 255 for position in range(self.send(2006))).decode("utf-8")
        return [
            text,
            *[self.send(message) for message in (2009, 2008, 2159, 2174, 2016)],
            [[self.send(2579, index), self.send(2577, index)] for index in range(self.send(2570))],
        ]

    def capture_case(self, before, position, character, readonly, anchor, extra_anchor, extra_caret, overtype):
        self.send(2171, 0)
        self.send(2422, 0)
        self.send(2782, 0)
        data = before.encode()
        assert self.kernel.WriteProcessMemory(self.handle, self.memory, data + b"\0", len(data) + 1, None)
        self.send(2181, 0, self.memory)
        self.send(2160, anchor, position)
        if extra_caret >= 0:
            self.send(2573, extra_caret, extra_anchor)
        self.send(2186, int(overtype))
        self.command(40004)
        self.send(2175)
        self.send(2014)
        self.send(2782, 3)
        self.send(4003, 0, -1)
        self.send(2171, int(readonly))
        self.user.SendMessageW(self.editor, 0x102, ord(character), 0)
        after = self.state()
        undo = []
        while self.send(2174):
            self.send(2176)
            undo.append(self.state())
        redo = []
        while self.send(2016):
            self.send(2011)
            redo.append(self.state())
        self.command(40004)
        return {"after": after, "undo": undo, "redo": redo}

    def close(self):
        self.send(2171, 0)
        self.send(2175)
        self.user.SendMessageW(self.frame, 0x10, 0, 0)
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            raise
        self.kernel.VirtualFreeEx(self.handle, self.memory, 0, 0x8000)
        self.kernel.CloseHandle(self.handle)
