"""Capture pinned Notepad4 header commands on an isolated Windows desktop."""
import ctypes as c
from ctypes import wintypes as w
import csv
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

exe, output = map(Path, sys.argv[1:3])
extra = len(sys.argv) > 3 and sys.argv[3] == "--extra"
redo_only = len(sys.argv) > 3 and sys.argv[3] == "--redo"
output.mkdir(parents=True, exist_ok=True)
host = output / "original-portable"
host.mkdir(exist_ok=True)
shutil.copy2(exe, host / "Notepad4.exe")
(host / "Notepad4.ini").write_text("[Notepad4]\n[Settings]\nSaveSettings=0\nLoadASCIIasUTF8=0\nLoadANSIasUTF8=0\nNoEncodingTags=0\nSkipUnicodeDetection=0\n", encoding="utf-8")
u, k = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True)
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
desktop_name = "inherited-private-desktop"
rows, saved = [], []
try:
    for language, suffix, initial in [(language, suffix, initial)
        for language, suffix in [("PlainText", "txt"), ("Cpp", "cpp"), ("Python", "py"), ("Json", "json")]
        for initial in (["utf16", "gbk"] if extra else [None])]:
        path = host / f"input.{suffix}"
        initial_text = "# coding: GBK\r\n中文\r\nA\r\nB\r\nC" if initial == "gbk" else "A\r\nB\r\nC"
        path.write_bytes(initial_text.encode("gbk" if initial == "gbk" else "utf-16-le" if initial == "utf16" else "utf-8"))
        startup = subprocess.STARTUPINFO()
        process = subprocess.Popen([str(host / "Notepad4.exe"), "/ns", "/n", str(path)], startupinfo=startup)
        windows, editors, observed = [], [], []
        @callback
        def top(hwnd, _):
            pid, name = w.DWORD(), c.create_unicode_buffer(128)
            u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            u.GetClassNameW(hwnd, name, 128)
            if pid.value == process.pid: observed.append(name.value)
            if pid.value == process.pid and name.value == "Notepad4": windows.append(hwnd)
            return True
        @callback
        def child(hwnd, _):
            name = c.create_unicode_buffer(128)
            u.GetClassNameW(hwnd, name, 128)
            if name.value == "Scintilla": editors.append(hwnd)
            return True
        deadline = time.monotonic() + 15
        while not windows and time.monotonic() < deadline:
            u.EnumWindows(top, 0)
            time.sleep(.05)
        if len(windows) != 1:
            process.terminate()
            raise RuntimeError(f"Original window not found: {windows}, process={process.poll()}, classes={sorted(set(observed))}")
        frame = windows[0]
        u.EnumChildWindows(frame, child, 0)
        assert len(editors) == 1, editors
        editor = editors[0]
        print(f"Owned original {process.pid}, frame={frame}, editor={editor}", flush=True)
        handle = k.OpenProcess(0x38, False, process.pid)
        memory = k.VirtualAllocEx(handle, None, 65536, 0x3000, 4)
        assert memory
        def send(message, wp=0, lp=0): return u.SendMessageW(editor, message, wp, lp)
        def command(message): return u.SendMessageW(frame, 0x111, message, 0)
        def text(data):
            assert k.WriteProcessMemory(handle, memory, data + b"\0", len(data) + 1, None)
            send(2181, 0, memory)
        def state():
            data = bytes(send(2007, p) & 255 for p in range(send(2006)))
            return [data.hex(), *[send(m) for m in [2009, 2008, 2159, 2174, 2016]]]
        try:
            for encoding, command_id in ([(initial, None)] if extra else
                [("utf8", 40103), ("utf8bom", 40104), ("utf16bom", 40101), ("ansi", 40100)]):
                print(f"Capturing {language} {encoding}", flush=True)
                send(2171, 0)
                text(b"")
                send(2175)
                if command_id is not None: command(command_id)
                for eol, data in [(0, b"A\r\nB\r\nC"), (1, b"A\rB\rC"), (2, b"A\nB\nC")]:
                    starts = [0, 1, 1 + (2 if eol == 0 else 1), 2 + 2 * (2 if eol == 0 else 1)]
                    for name, anchor, caret, rectangle, readonly in ([("protected_redo", 4, 1, 0, 1)] if redo_only else [
                        ("start", 0, 0, 0, 0), ("column1", 1, 1, 0, 0),
                        ("second", starts[2], starts[2], 0, 0), ("third", starts[3], starts[3], 0, 0),
                        ("forward", 0, 3, 0, 0), ("reverse", 3, 0, 0, 0),
                        ("rectangle", 0, starts[2] + 1, 1, 0), ("protected", 0, 0, 0, 1)]):
                        for operation, cmd in [("encoding", 40349), ("shebang", 40388)]:
                            send(2171, 0)
                            send(2422, 0)
                            text(data)
                            send(2031, eol)
                            send(2160, anchor, caret)
                            if rectangle:
                                send(2422, 1)
                                send(2590, anchor)
                                send(2588, caret)
                            send(2175)
                            send(2014)
                            if redo_only:
                                assert k.WriteProcessMemory(handle, memory, b"redo\0", 5, None)
                                send(2170, 0, memory)
                                send(2176)
                                assert send(2016), "original protected scenario starts with redo available"
                            send(2171, readonly)
                            command(cmd)
                            after = state()
                            send(2176)
                            undo = state()
                            send(2011)
                            redo = state()
                            rows.append([language, encoding, eol, name, operation, data.hex(), anchor, caret,
                                         rectangle, readonly, *after, *undo, *redo])
                            if name == "start" and eol == 0 and not readonly:
                                command(40004)
                                saved.append(dict(language=language, encoding=encoding, operation=operation,
                                                  utf8=after[0], bytes=path.read_bytes().hex()))
        finally:
            send(2171, 0)
            send(2014)
            u.SendMessageW(frame, 0x10, 0, 0)
            process.wait(timeout=10)
            k.VirtualFreeEx(handle, memory, 0, 0x8000)
            k.CloseHandle(handle)
finally:
    pass
prefix = "header-redo" if redo_only else "header-extra" if extra else "header"
with (output / f"{prefix}-reference.tsv").open("w", encoding="utf-8", newline="") as stream:
    writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
    writer.writerow(["language", "encoding", "eol", "case", "command", "input", "anchor", "caret", "rectangle", "readonly",
                     *[f"{phase}_{field}" for phase in ["after", "undo", "redo"]
                       for field in ["hex", "anchor", "caret", "modified", "can_undo", "can_redo"]]])
    writer.writerows(rows)
(output / ("original-redo.json" if redo_only else "original-extra.json" if extra else "original.json")).write_text(json.dumps(dict(revision="89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
    exe=str(exe), desktop=desktop_name, cases=len(rows), saved=saved), indent=2), encoding="utf-8")
print(f"Captured {len(rows)} original cases and {len(saved)} saved-byte fixtures")
