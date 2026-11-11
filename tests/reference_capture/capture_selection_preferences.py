"""Capture pinned original selection preference edges on a private Windows desktop."""
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import time


def find_editor_windows(user, process, callback):
    top = []
    def window(hwnd, _):
        pid = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, c.byref(pid))
        if pid.value == process.pid:
            name = c.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, 128)
            if name.value == "Notepad4":
                top.append(hwnd)
        return True
    deadline = time.monotonic() + 10
    while not top and time.monotonic() < deadline:
        user.EnumWindows(callback(window), 0)
        time.sleep(.05)
    if not top:
        raise RuntimeError("original Notepad4 window was not found")
    children = []
    def child(hwnd, _):
        name = c.create_unicode_buffer(128)
        user.GetClassNameW(hwnd, name, 128)
        if name.value == "Scintilla":
            children.append(hwnd)
        return True
    user.EnumChildWindows(top[0], callback(child), 0)
    return top[0], children[0]


def main():
    user = c.WinDLL("user32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    user.SendMessageW.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
    user.SendMessageW.restype = c.c_ssize_t
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, c.c_ssize_t)
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.EnumDesktopWindows.argtypes = [w.HANDLE, callback, c.c_ssize_t]
    user.CloseDesktop.argtypes = [w.HANDLE]
    desktop_name = "wxnotepad-selection-reference-" + str(__import__("os").getpid())
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x1ff, None)
    if not desktop:
        raise c.WinError(c.get_last_error())
    user.EnumWindows = lambda cb, param: user.EnumDesktopWindows(desktop, cb, param)
    user.EnumChildWindows.argtypes = [w.HWND, callback, c.c_ssize_t]
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.VirtualAllocEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD]
    kernel.VirtualAllocEx.restype = c.c_void_p
    kernel.WriteProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
    kernel.VirtualFreeEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    output = Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="selection-original-") as temp:
        path = Path(temp) / "fixture.txt"
        path.write_text("\ufeff", encoding="utf-8")
        executable = Path(temp) / "Notepad4.exe"
        shutil.copy2(sys.argv[1], executable)
        (Path(temp) / "Notepad4.ini").write_text("[Settings]\nSelectOption=15\nWordWrap=0\n", encoding="utf-16")
        class Startup(c.Structure):
            _fields_ = [("cb", w.DWORD), ("lpReserved", w.LPWSTR), ("lpDesktop", w.LPWSTR),
                        ("lpTitle", w.LPWSTR), ("dwX", w.DWORD), ("dwY", w.DWORD),
                        ("dwXSize", w.DWORD), ("dwYSize", w.DWORD), ("dwXCountChars", w.DWORD),
                        ("dwYCountChars", w.DWORD), ("dwFillAttribute", w.DWORD), ("dwFlags", w.DWORD),
                        ("wShowWindow", w.WORD), ("cbReserved2", w.WORD), ("lpReserved2", c.c_void_p),
                        ("hStdInput", w.HANDLE), ("hStdOutput", w.HANDLE), ("hStdError", w.HANDLE)]
        class ProcessInfo(c.Structure):
            _fields_ = [("handle", w.HANDLE), ("thread", w.HANDLE), ("pid", w.DWORD), ("tid", w.DWORD)]
        startup = Startup()
        startup.cb = c.sizeof(startup)
        startup.lpDesktop = desktop_name
        process = ProcessInfo()
        kernel.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p, w.BOOL,
                                        w.DWORD, c.c_void_p, w.LPCWSTR, c.POINTER(Startup), c.POINTER(ProcessInfo)]
        arguments = c.create_unicode_buffer(subprocess.list2cmdline([str(executable), "/n", str(path)]))
        if not kernel.CreateProcessW(None, arguments, None, None, False, 0, None, str(Path(temp)), c.byref(startup), c.byref(process)):
            raise c.WinError(c.get_last_error())
        frame, editor = find_editor_windows(user, process, callback)
        handle = kernel.OpenProcess(0x38, False, process.pid)
        remote = kernel.VirtualAllocEx(handle, None, 65536, 0x3000, 4)
        if not remote:
            raise c.WinError(c.get_last_error())
        def send(message, first=0, second=0):
            return user.SendMessageW(editor, message, first, second)
        def text(value):
            encoded = value.encode("utf-8") + b"\0"
            if not kernel.WriteProcessMemory(handle, remote, encoded, len(encoded), None):
                raise c.WinError(c.get_last_error())
            send(2004)
            send(2001, len(encoded) - 1, remote)
            send(2175)
            send(2014)
        def state():
            length = send(2006)
            send(2182, length + 1, remote)
            data = c.create_string_buffer(length + 1)
            if not kernel.ReadProcessMemory(handle, remote, data, length + 1, None):
                raise c.WinError(c.get_last_error())
            return {"text": data.raw[:length].decode("utf-8"), "anchor": send(2009),
                    "caret": send(2008), "modified": send(2159), "undo": send(2174), "redo": send(2016),
                    "selections": send(2570), "multiple": send(2564), "history": send(2783),
                    "first_line": send(2152), "x_offset": send(2398)}
        def replace(value):
            encoded = value.encode("utf-8") + b"\0"
            kernel.WriteProcessMemory(handle, remote, encoded, len(encoded), None)
            send(2170, 0, remote)
        def command(identifier):
            user.SendMessageW(frame, 0x111, identifier, 0)
        results = {}
        try:
            text("abcd" + chr(13) + chr(10) + "efgh" + chr(13) + chr(10) + "ijkl")
            send(2160, 1, 8)
            send(2422, 2)
            results["line-before"] = state()
            user.SendMessageW(editor, 0x102, ord("L"), 1)
            results["line-replace"] = state()
            text("abcd\nefgh\nijkl")
            send(2160, 2, 1)
            send(2573, 7, 6)
            results["multiple-before"] = state()
            command(40080)
            results["multiple-disabled-active"] = state()
            replace("X")
            results["multiple-disabled-replace"] = state()
            send(2176)
            results["multiple-disabled-undo"] = state()
            command(40080)
            for enabled in (True, False):
                if not enabled:
                    command(40083)
                for reverse in (False, True):
                    text(("0123456789" * 12 + chr(10)) * 80)
                    send(2160, 2426 if reverse else 2422, 2422 if reverse else 2426)
                    send(2613, 15)
                    send(2397, 20)
                    replace("X")
                    send(2160, 615, 615)
                    send(2613, 5)
                    results[f"history-{enabled}-{reverse}-edited"] = state()
                    send(2176)
                    results[f"history-{enabled}-{reverse}-undo"] = state()
                    send(2011)
                    results[f"history-{enabled}-{reverse}-redo"] = state()
            command(40083)
            text("0123456789" * 12 + chr(10))
            send(2160, 6, 2)
            replace("X")
            results["history-toggle-before"] = state()
            command(40083)
            results["history-toggle-off"] = state()
            command(40083)
            results["history-toggle-on"] = state()
            send(2176)
            results["history-toggle-undo"] = state()
            send(2011)
            results["history-toggle-redo"] = state()
            (output / "original-selection-preferences.json").write_text(json.dumps({
                "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a", "cases": results
            }, indent=2) + "\n", encoding="utf-8")
            print(f"Captured {len(results)} original runtime preference states on private desktop")
        finally:
            send(2171, 0)
            send(2014)
            kernel.VirtualFreeEx(handle, remote, 0, 0x8000)
            kernel.CloseHandle(handle)
            user.SendMessageW(frame, 0x10, 0, 0)
            kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
            if kernel.WaitForSingleObject(process.handle, 10000) != 0:
                raise RuntimeError("owned original process must exit")
            kernel.CloseHandle(process.handle)
            kernel.CloseHandle(process.thread)
            user.CloseDesktop(desktop)


if __name__ == "__main__":
    main()
