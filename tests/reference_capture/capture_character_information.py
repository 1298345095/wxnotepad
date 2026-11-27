"""Capture Show Hex/Character Info from an owned original; writes the Windows clipboard.

Run only with current user clipboard authorization and the shared native GUI slot.
The bounded batch publishes Character Info for six characters and three encodings.
"""

import argparse
import ctypes as c
import hashlib
import json
import os
import shutil
import time
from ctypes import wintypes as w
from pathlib import Path

from original_host import CALLBACK, Original


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    portable = args.output / "original-portable"
    portable.mkdir()
    executable = portable / "Notepad4.exe"
    shutil.copy2(args.executable, executable)
    (portable / "Notepad4.ini").write_text("[Settings]\nSaveSettings=0\nWordWrap=0\n", encoding="utf-16")
    user, kernel = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True)
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.OpenClipboard.argtypes = [w.HWND]
    user.GetClipboardData.argtypes = [w.UINT]
    user.GetClipboardData.restype = w.HANDLE
    kernel.GlobalLock.argtypes = [w.HANDLE]
    kernel.GlobalLock.restype = c.c_void_p
    kernel.GlobalUnlock.argtypes = [w.HANDLE]
    desktop_name = f"wxnotepad-character-info-{os.getpid()}"
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x1FF, None)
    assert desktop
    original = Original(None, executable, desktop, desktop_name)
    result = {
        "source_revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
        "executable_source": str(args.executable.resolve()),
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "hex": [],
        "info": [],
    }

    def info():
        original.command(40399)
        assert user.OpenClipboard(None)
        try:
            memory = user.GetClipboardData(13)
            assert memory
            pointer = kernel.GlobalLock(memory)
            assert pointer
            try:
                return c.wstring_at(pointer)
            finally:
                kernel.GlobalUnlock(memory)
        finally:
            user.CloseClipboard()

    try:
        for name, text in [
            ("ascii", b"AB"),
            ("cjk", "中".encode()),
            ("supplementary", "😀".encode()),
            ("controls-nul", b"A\0\t\r\nB"),
        ]:
            print("hex", name, flush=True)
            for reverse in (False, True):
                original.reset(text, len(text) if reverse else 0, 0 if reverse else len(text))
                before = original.state()
                assert bytes.fromhex(before["hex"]) == text, "original reset completed before fixture"
                original.command(40467)
                after = original.state()
                original.send(2176)
                undo = original.state()
                original.send(2011)
                result["hex"].append(
                    {
                        "name": name,
                        "reverse": reverse,
                        "before": before,
                        "after": after,
                        "undo": undo,
                        "redo": original.state(),
                    }
                )
        original.reset(b"AB CD", 0, 2)
        original.send(2563, 1)
        original.send(2573, 5, 3)
        original.command(40467)
        result["multiple"] = original.state()
        result["edge_cases"] = []
        for name, readonly, rectangle, anchor, caret in [
            ("empty", False, False, 1, 1),
            ("readonly", True, False, 0, 2),
            ("rectangle", False, True, 0, 3),
        ]:
            original.reset(b"A\nB", anchor, caret)
            if readonly:
                original.send(2171, 1)
            if rectangle:
                original.send(2422, 1)
                original.send(2590, 0)
                original.send(2588, 3)
            before = original.state()
            if rectangle:
                assert user.PostMessageW(original.hwnd, 0x111, 40467, 0)
                accepted = []

                @CALLBACK
                def accept(hwnd, unused):
                    pid = w.DWORD()
                    user.GetWindowThreadProcessId(hwnd, c.byref(pid))
                    name = c.create_unicode_buffer(64)
                    user.GetClassNameW(hwnd, name, 64)
                    if pid.value == original.process.pid and name.value == "#32770":
                        user.PostMessageW(hwnd, 0x111, 1, 0)
                        accepted.append(hwnd)
                    return True

                deadline = time.monotonic() + 10
                while not accepted and time.monotonic() < deadline:
                    user.EnumDesktopWindows(desktop, accept, 0)
                    time.sleep(0.02)
                assert accepted, "original rectangle warning appeared and was accepted"
            else:
                original.command(40467)
            after = original.state()
            assert after["hex"] == before["hex"] and not after["modified"] and not after["undo"]
            if rectangle:
                assert after == before, "original rectangle rejection preserves document, selection and history"
            result["edge_cases"].append({"name": name, "before": before, "after": after})
        for name, text, anchor, caret in [
            ("ascii", b"A", 0, 1),
            ("cjk", "中".encode(), 3, 0),
            ("supplementary", "😀".encode(), 0, 4),
            ("tab", b"\t", 0, 0),
            ("nul", b"\0", 0, 0),
            ("end", b"A", 1, 1),
        ]:
            original.reset(text, anchor, caret)
            print("info", name, flush=True)
            before = original.state()
            payload = info()
            assert original.state() == before, "Character Info preserves original editor state"
            result["info"].append({"name": name, "before": before, "after": original.state(), "payload": payload})
        for name, encoding, text in [
            ("gbk", "gbk", "中"),
            ("windows1252", "windows-1252", "é"),
            ("utf16le", "utf-16le", "中"),
        ]:
            original.close()
            print("encoding", name, flush=True)
            path = args.output / (name + ".txt")
            path.write_bytes(text.encode("utf-16") if name == "utf16le" else text.encode(encoding))
            # Original /e argument is its named explicit source decoding entrypoint.
            original = Original(path, executable, desktop, desktop_name, encoding)
            original.send(2160, 0, original.send(2006))
            before = original.state()
            assert before["hex"], "original initial source load completed before selection"
            payload = info()
            original.command(40467)
            result["hex"].append({"name": name, "before": before, "after": original.state(), "payload": payload})
        original.close()
        path = args.output / "ansi.txt"
        path.write_bytes("é中".encode("mbcs", errors="replace"))
        original = Original(path, executable, desktop, desktop_name, "ansi")
        original.send(2160, 0, original.send(2006))
        before = original.state()
        original.command(40467)
        result["hex"].append({"name": "native-ansi", "before": before, "after": original.state()})
        (args.output / "samples.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
    finally:
        original.close()
        user.CloseDesktop(desktop)


if __name__ == "__main__":
    main()
