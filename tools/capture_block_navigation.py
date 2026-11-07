"""Capture native block commands from an owned original process, without clipboard use."""
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

DOCUMENTS = [
    ("C/C++", "cpp", "void outer() {\n\tif (a) {\n\t\twork();\n\t}\n\tif (b) {\n\t\tmore();\n\t}\n}\n \t\nvoid last() {\n\tdone();\n}"),
    ("Python", "py", "def outer():\n\tif a:\n\t\twork()\n\tif b:\n\t\tmore()\n\t\n\tdone()\n\n \t\ndef last():\n\tpass"),
    ("JSON", "json", '{\n\t"first": {\n\t\t"items": [\n\t\t\t1, 2\n\t\t]\n\t},\n\t"next": {\n\t\t"x": 3\n\t}\n}\n \t'),
    ("Plain Text", "txt", "top\n  body\n\nlast"),
    ("C/C++", "cpp", "void long_block() {\n" + "\t// 中文😀 long body\n" * 80 + "}\nvoid sibling() {\n}"),
]
COMMANDS = [40485, 40486, 40487, 40488, 40489, 40490, 40252]

def main():
    source, output, header = map(Path, sys.argv[1:4])
    portable = output / "original-portable"
    portable.mkdir(parents=True, exist_ok=True)
    executable = portable / "Notepad4.exe"
    shutil.copy2(source, executable)
    (portable / "Notepad4.ini").write_text("[Settings]\nSaveSettings=0\nWordWrap=0\nLineSelection=1\nTabWidth=4\n", encoding="utf-16")
    user, kernel = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True)
    user.SendMessageW.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
    user.SendMessageW.restype = c.c_ssize_t
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, c.c_ssize_t)
    user.EnumWindows.argtypes = [callback, c.c_ssize_t]
    user.EnumChildWindows.argtypes = [w.HWND, callback, c.c_ssize_t]
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.VirtualAllocEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD]
    kernel.VirtualAllocEx.restype = c.c_void_p
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
    kernel.VirtualFreeEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    results, documents = [], []
    for language, extension, text in DOCUMENTS:
        path = portable / f"fixture.{extension}"
        path.write_bytes(b"\xef\xbb\xbf" + text.encode("utf-8"))
        process = subprocess.Popen([str(executable), "/n", "/ns", str(path)])
        windows = {}
        def top(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, 128)
            if pid.value == process.pid and name.value == "Notepad4": windows["frame"] = hwnd
            return True
        deadline = time.monotonic() + 10
        while "frame" not in windows and time.monotonic() < deadline:
            user.EnumWindows(callback(top), 0)
            time.sleep(.02)
        if "frame" not in windows: raise RuntimeError("owned original frame was not found")
        def child(hwnd, _):
            name = c.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, 128)
            if name.value == "Scintilla": windows["editor"] = hwnd
            return True
        user.EnumChildWindows(windows["frame"], callback(child), 0)
        handle = kernel.OpenProcess(0x38, False, process.pid)
        remote = kernel.VirtualAllocEx(handle, None, 65536, 0x3000, 4)
        if not remote: raise c.WinError(c.get_last_error())
        def send(message, first=0, second=0): return user.SendMessageW(windows["editor"], message, first, second)
        def command(identifier): user.SendMessageW(windows["frame"], 0x111, identifier, 0)
        def state():
            length = send(2006)
            send(2182, length + 1, remote)
            data = c.create_string_buffer(length + 1)
            if not kernel.ReadProcessMemory(handle, remote, data, length + 1, None): raise c.WinError(c.get_last_error())
            return dict(text=data.raw[:length].decode("utf-8"), anchor=send(2009), caret=send(2008),
                selection_mode=send(2423), selections=send(2570), modified=send(2159), undo=send(2174), redo=send(2016),
                anchor_virtual=send(2595), caret_virtual=send(2593), read_only=send(2140),
                codepage=send(2137), visible=[send(2228, line) for line in range(send(2154))],
                expanded=[send(2230, line) for line in range(send(2154))])
        try:
            send(2036, 4)
            send(4003, 0, -1)
            time.sleep(.08)
            documents.append(dict(language=language, text=text, levels=[send(2223, line) for line in range(send(2154))], initial=state()))
            assert state()["text"] == text
            send(2004)
            send(2176)
            send(2014)
            for line in range(send(2154)):
                if len(documents) == 5 and line not in [0, 40, 81, 82, 83]: continue
                position = min(send(2167, line) + 2, send(2136, line))
                for index, identifier in enumerate(COMMANDS):
                    for mode in (range(4) if identifier == 40252 else [1]):
                        for direction in ([0, 1, -1] if identifier == 40252 else [0]):
                            for setup in ([0, 1, 2, 3, 4] if line == 2 else [0]):
                                send(2171, 0)
                                send(2662, 1)
                                send(2422, 0)
                                command(40076 + mode)
                                anchor = position if direction == 0 else (0 if direction == 1 else len(text.encode()))
                                send(2160, anchor, position)
                                if setup == 1:
                                    send(2662, 0)
                                    send(2160, anchor, position)
                                elif setup == 2: send(2171, 1)
                                elif setup == 3:
                                    send(2422, 1)
                                    send(2590, anchor)
                                    send(2588, position)
                                    send(2594, 3)
                                    send(2592, 2)
                                elif setup == 4: send(2573, 0, 0)
                                before = state()
                                command(identifier)
                                after = state()
                                assert after["text"] == text and all(after[key] == before[key] for key in ["modified", "undo", "redo", "codepage"]), (language, line, index, mode, setup, after)
                                results.append(dict(document=len(documents)-1, command=index, mode=mode, setup=setup,
                                    anchor=anchor, caret=position, before=before, after=after))
            send(2171, 0)
            command(40401)
            send(4003, 0, -1)
            documents[-1]["default_code_style"] = dict(levels=[send(2223, line) for line in range(send(2154))], state=state())
        finally:
            send(2171, 0)
            send(2014)
            kernel.VirtualFreeEx(handle, remote, 0, 0x8000)
            kernel.CloseHandle(handle)
            user.SendMessageW(windows["frame"], 0x10, 0, 0)
            process.wait(timeout=10)
    (output / "original.json").write_text(json.dumps(dict(revision="89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
        documents=documents, fixtures=results), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    rows = ["// Captured from original Notepad4 89b8b36 by tools/capture_block_navigation.py.", "#pragma once", "namespace BlockReference {",
        "struct Document { const char* language; const char* text; };", "inline constexpr Document documents[] = {"]
    for document in documents:
        rows.append("    {" + json.dumps(document["language"]) + ", " + json.dumps(document["text"], ensure_ascii=False) + "},")
    rows += ["};", "struct Fixture { int document, command, mode, setup; long anchor, caret, expectedAnchor, expectedCaret; int selectionMode, selections, anchorVirtual, caretVirtual; const char* visible; const char* expanded; };", "inline constexpr Fixture fixtures[] = {"]
    for row in results:
        after = row["after"]
        values = [row[key] for key in ["document", "command", "mode", "setup", "anchor", "caret"]]
        values += [after[key] for key in ["anchor", "caret", "selection_mode", "selections", "anchor_virtual", "caret_virtual"]]
        visible = json.dumps("".join(map(str, after["visible"])))
        expanded = json.dumps("".join(map(str, after["expanded"])))
        rows.append("    {" + ", ".join(map(str, values)) + ", " + visible + ", " + expanded + "},")
    rows += ["};", "}", ""]
    header.write_text("\n".join(rows), encoding="utf-8")
    print(f"Captured {len(results)} original block command cases")

if __name__ == "__main__": main()
