"""Capture number-conversion literals from the pinned original Windows process."""
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time


def main():
    user = c.WinDLL("user32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    user.SendMessageW.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
    user.SendMessageW.restype = c.c_ssize_t
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, c.c_ssize_t)
    user.EnumWindows.argtypes = [callback, c.c_ssize_t]
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
    with tempfile.TemporaryDirectory(prefix="radix-original-") as temp:
        path = Path(temp) / "fixture.txt"
        path.write_text("\ufeff", encoding="utf-8")
        process = subprocess.Popen([sys.argv[1], "/n", str(path)])
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
        editor = children[0]
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
                    "selections": send(2570)}
        samples = [
            "0 1 255 256 65535 18446744073709551615 18446744073709551616 18446744073709551617",
            "0xFF 0Xff 0o17 0O17 0b101 0B101 0009 1_024 0_12 0__7",
            "0x 0X_ 0o8 0b2 0xG 0b_ 0o_ 0_x1 0z99",
            "abc123 _123 a0xFF 123abc 0xFFtail 0b102 0o789 -12 +34 .56 1.2e3",
            "0xffffffffffffffff 0x10000000000000000 0o2000000000000000000000 0b1" + "0" * 64,
            "中12 😀34\r\n56\r78\n90", "0x10 0b10 0O10 10", "0", "plain text", "12\0 34",
        ]
        results = []
        try:
            for name, command in [("To Hexadecimal", 40394), ("To Decimal", 40392),
                                  ("To Binary", 40391), ("To Octet", 40393)]:
                for sample in samples:
                    for reverse in [False, True]:
                        text(sample)
                        size = send(2006)
                        send(2160, size if reverse else 0, 0 if reverse else size)
                        user.SendMessageW(top[0], 0x111, command, 0)
                        result = {"command": name, "before": sample, "reverse": reverse,
                                  "after": state()}
                        send(2176)
                        result["undo"] = state()
                        send(2011)
                        result["redo"] = state()
                        results.append(result)
            for label, setup in [("empty", lambda: send(2160, 3, 3)),
                                 ("multiple", lambda: (send(2160, 2, 0), send(2573, 7, 5))),
                                 ("read-only", lambda: (send(2160, 0, 2), send(2171, 1)))]:
                text("12 a 34")
                setup()
                user.SendMessageW(top[0], 0x111, 40394, 0)
                results.append({"case": label, "after": state()})
                send(2171, 0)
            text("plain text")
            send(2001, 1, remote)
            send(2176)
            send(2160, 10, 0)
            user.SendMessageW(top[0], 0x111, 40394, 0)
            results.append({"case": "unchanged-pending-redo", "after": state()})
            text("12\n34")
            send(2422, 1)
            send(2590, 0)
            send(2588, 5)
            command_thread = threading.Thread(target=lambda: user.SendMessageW(top[0], 0x111, 40394, 0))
            command_thread.start()
            dialogs = []
            def dialog(hwnd, _):
                pid = w.DWORD()
                user.GetWindowThreadProcessId(hwnd, c.byref(pid))
                name = c.create_unicode_buffer(128)
                user.GetClassNameW(hwnd, name, 128)
                if pid.value == process.pid and name.value == "#32770":
                    dialogs.append(hwnd)
                return True
            deadline = time.monotonic() + 5
            while not dialogs and time.monotonic() < deadline:
                user.EnumWindows(callback(dialog), 0)
                time.sleep(.02)
            if not dialogs:
                raise RuntimeError("original rectangle notification was not observed")
            user.SendMessageW(dialogs[0], 0x111, 1, 0)
            command_thread.join(timeout=5)
            results.append({"case": "rectangle-notification", "after": state()})
            send(2422, 0)
            (output / "original-fixtures.json").write_text(json.dumps({
                "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a", "fixtures": results
            }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            if len(sys.argv) > 3:
                def literal(value):
                    return json.dumps(value, ensure_ascii=False).replace("\\u0000", "\\0")
                rows = ["// Captured from Notepad4 89b8b36 by tools/capture_number_radix.py.",
                        "// Original Windows process, WM_COMMAND and Scintilla public messages.",
                        "#pragma once", "#include <string_view>", "namespace NumberRadixReference {",
                        "struct Fixture { const char* command; std::string_view before; std::string_view after; };",
                        "inline constexpr Fixture fixtures[] = {"]
                for result in results:
                    if "command" in result and not result["reverse"]:
                        before, after = result["before"], result["after"]["text"]
                        rows.append(f'    {{{literal(result["command"])}, '
                                    f'{{{literal(before)}, {len(before.encode("utf-8"))}}}, '
                                    f'{{{literal(after)}, {len(after.encode("utf-8"))}}}}},')
                rows.extend(["};", "}", ""])
                Path(sys.argv[3]).write_text("\n".join(rows), encoding="utf-8")
            print(f"Captured {len(results)} original runtime cases")
        finally:
            send(2171, 0)
            send(2014)
            kernel.VirtualFreeEx(handle, remote, 0, 0x8000)
            kernel.CloseHandle(handle)
            user.SendMessageW(top[0], 0x10, 0, 0)
            process.wait(timeout=10)


if __name__ == "__main__":
    main()
