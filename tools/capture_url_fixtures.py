"""Capture URL commands from the pinned original Windows editor (no desktop input)."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import platform
from pathlib import Path
import subprocess
import time


def capture(executable: Path, output: Path) -> None:
    user = c.WinDLL("user32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    send = user.SendMessageW
    send.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
    send.restype = c.c_ssize_t
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.VirtualAllocEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD]
    kernel.VirtualAllocEx.restype = c.c_void_p
    kernel.WriteProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
    kernel.VirtualFreeEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([str(executable), "/ns", "/n"], startupinfo=startup)
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, c.c_ssize_t)
    windows = []

    @callback
    def top(hwnd, _):
        pid = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, c.byref(pid))
        if pid.value == process.pid:
            name = c.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, 128)
            if name.value == "Notepad4":
                windows.append(hwnd)
        return True

    deadline = time.monotonic() + 10
    while not windows and time.monotonic() < deadline:
        user.EnumWindows(top, 0)
        time.sleep(0.05)
    if len(windows) != 1:
        process.terminate()
        raise RuntimeError(f"expected owned original Notepad4 window: {windows}")
    frame = windows[0]
    children = []

    @callback
    def child(hwnd, _):
        name = c.create_unicode_buffer(128)
        user.GetClassNameW(hwnd, name, 128)
        if name.value == "Scintilla":
            children.append(hwnd)
        return True

    user.EnumChildWindows(frame, child, 0)
    if len(children) != 1:
        process.terminate()
        raise RuntimeError(f"expected owned original Scintilla: {children}")
    editor = children[0]
    handle = kernel.OpenProcess(0x38, False, process.pid)
    memory = kernel.VirtualAllocEx(handle, None, 65536, 0x3000, 0x04)
    if not memory:
        process.terminate()
        raise c.WinError(c.get_last_error())

    def write(data):
        if not kernel.WriteProcessMemory(handle, memory, data, len(data), None):
            raise c.WinError(c.get_last_error())

    def state():
        size = send(editor, 2006, 0, 0)
        text = bytes(send(editor, 2007, pos, 0) & 255 for pos in range(size))
        return dict(hex=text.hex(), anchor=send(editor, 2009, 0, 0),
                    caret=send(editor, 2008, 0, 0), modified=send(editor, 2159, 0, 0),
                    undo=send(editor, 2174, 0, 0), redo=send(editor, 2016, 0, 0))

    inputs = [
        ("unicode", "中文 café 😀 𐐀".encode()),
        ("punctuation", b"https://user:pass@host/a b\\c?x=1&y=2#frag"),
        ("reserved", b" !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"),
        ("existing", b"a%20b+%2f%2520"),
        ("malformed", b"% %0 %G0 %0G %1z %zz %a %ABC %u0041"),
        ("invalid_utf8", b"%80%C0%AF%ED%A0%80%F4%90%80%80%E2%82"),
        ("trim", b" \x07\b\f\n\r\t\vA \tB\x07 \x07\b\f\n\r\t\v"),
        ("whitespace", b" \x07\b\f\n\r\t\v"),
        ("controls", bytes(range(1, 33)) + b"a" + bytes(range(1, 33))),
        ("nul_input", b"a\0b%20c"),
        ("nul_leading", b"\0a%20b"),
        ("nul_decoded", b"a%00b%20c"),
        ("unchanged", b"abc-_.~"),
    ]
    punctuation = "!\"$&'()*+,-./:;<=>@[\\]^_`{|}~"
    inputs += [(f"ascii_{ord(char):02x}", ("a" + char + "b").encode()) for char in punctuation]
    inputs += [(f"url_{index}", text.encode()) for index, text in enumerate([
        "a#f?q", "a?x#f", "a#f#x?q?y", "a#x?y#z", "a?x#y?z", "a?中文 x#é y",
        "HTTP://User:Pass@HOST:80/a", "http://host", "http://a b/x y", "http://中文/路径",
        "http:a b\\c", "http:\\host\\a", "file:a b\\c", "file://C:/a b", "mailto:?a b#c d",
        "mailto://host", "ftp:a b\\c", "foo:a b\\c", "foo://a b\\c", "foo://host?x", "//host",
        "\\\\host\\a", "c://host", "a%00", "%E2%82", "%C2x%A9", "%C2é%A9",
        "%ED%A0%80", "%F4%90%80%80", "%E0%80%80", "%F0%80%80%80", "%C0%AF",
        "%F5%80%80%80", "%E2%82%41", "%EF%BF%BF", "foo:é", "FOO://host/x",
    ])]
    inputs += [(f"nul_boundary_{size}", b"%00" + b"x" * (size - 3)) for size in [14, 15, 16, 30, 31, 32]]
    inputs += [("nul_boundary_unicode", "中文😀%00abcde".encode())]
    inputs += [(f"scheme_{scheme}_{index}", (scheme + ':' + tail).encode())
               for scheme in ["http", "https", "file", "ftp", "news", "nntp", "gopher", "telnet", "wais",
                              "mailto", "about", "javascript", "vbscript", "res", "shell", "unknown"]
               for index, tail in enumerate(["a/b\\c?d e#f g", "//host/a\\b?c d#e f"])]
    result = {"revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a", "executable": str(executable),
              "platform": platform.platform(), "fixtures": []}
    try:
        send(editor, 2037, 65001, 0)
        for name, data in inputs:
            for command, command_id in [("encode", 40356), ("component", 40357), ("decode", 40343)]:
                send(editor, 2171, 0, 0)
                write(b"\0")
                send(editor, 2181, 0, memory)
                write(data)
                send(editor, 2001, len(data), memory)
                send(editor, 2160, 0, len(data))
                send(editor, 2175, 0, 0)
                send(editor, 2014, 0, 0)
                assert state()["hex"] == data.hex(), "original setup retains every input byte"
                send(frame, 0x111, command_id, 0)
                after = state()
                send(editor, 2176, 0, 0)
                undo = state()
                assert undo["hex"] == data.hex(), "one original Undo restores the exact input"
                send(editor, 2011, 0, 0)
                redo = state()
                result["fixtures"].append(dict(name=name, command=command, input=data.hex(), after=after,
                                               undone=undo, redone=redo))
    finally:
        send(editor, 2014, 0, 0)
        send(frame, 0x10, 0, 0)
        process.wait(timeout=5)
        kernel.VirtualFreeEx(handle, memory, 0, 0x8000)
        kernel.CloseHandle(handle)
    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    header = Path(__file__).resolve().parents[1] / "tests/url_reference_fixtures.h"
    lines = ["// Captured from Notepad4 89b8b36 on Windows 11 build 26200.",
             "// Regenerate with tools/capture_url_fixtures.py; see docs/observations/original-url-conversion.md.",
             "#pragma once", "namespace UrlReference {",
             "struct Fixture { const char* name; int command; const char* input; const char* expected;",
             "    int anchor; int caret; bool modified; int undoAnchor; int undoCaret; int redoAnchor; int redoCaret; };",
             "inline constexpr Fixture fixtures[] = {"]
    for row in result["fixtures"]:
        command = {"encode": 0, "component": 1, "decode": 2}[row["command"]]
        after = row["after"]
        lines.append(f'    {{"{row["name"]}", {command}, "{row["input"]}", "{after["hex"]}", '
                     f'{after["anchor"]}, {after["caret"]}, {str(bool(after["modified"])).lower()}, '
                     f'{row["undone"]["anchor"]}, {row["undone"]["caret"]}, '
                     f'{row["redone"]["anchor"]}, {row["redone"]["caret"]}}},')
    lines += ["};", "} // namespace UrlReference", ""]
    header.write_text("\n".join(lines), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    capture(args.executable, args.output)
