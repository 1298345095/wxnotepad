"""Capture original source options, readonly mode and missing-file decisions.

Uses only a separately copied original on an owned private Windows desktop.
"""

import argparse
import ctypes as c
import hashlib
import json
import shutil
import time
from ctypes import wintypes as w
from pathlib import Path

from original_host import CALLBACK, Original, kernel, user

SOURCES = (
    ("windows_1252", b"caf\xe9\r\n"),
    ("ibm437", b"\x82\x9c\r\n"),
    ("GB-2312", b"\xd6\xd0\xce\xc4\r\n"),
    ("csbig5", b"\xa4\xa4\xa4\xe5\r\n"),
    ("GB_18030", "中文😀\r\n".encode("gb18030")),
    ("uTf_8", "中文😀\r\n".encode("utf-8")),
    ("utf-16le", "中文😀\r\n".encode("utf-16-le")),
    ("utf-16be", "中文😀\r\n".encode("utf-16-be")),
    ("sHiFt_JiS", b"\xb6\xfb\xfc\r\n"),
    ("uHc", b"\xb0\xa1\x81\x41\r\n"),
    ("ANSI", b"native ASCII\r\n"),
    ("OEM", b"native ASCII\r\n"),
    ("ANSI", b"\xd6\xd0\xce\xc4\r\n"),
    ("ANSI", b"\x81"),
)


def controls(app):
    filename = c.create_unicode_buffer(32768)
    user.GetDlgItemTextW(app.hwnd, 0xFB05, filename, len(filename))
    status = user.GetDlgItem(app.hwnd, 0xFB00)
    assert status
    parts = []
    count = user.SendMessageW(status, 0x406, 0, 0)  # SB_GETPARTS.
    assert count > 0
    for index in range(count):
        length = user.SendMessageW(status, 0x40C, index, 0)  # SB_GETTEXTLENGTHW.
        if (length >> 16) & 0x1000:  # SBT_OWNERDRAW exposes application data instead of text.
            parts.append("<owner-drawn>")
            continue
        app.put(b"\0" * 2048)
        user.SendMessageW(status, 0x40D, index, app.memory)  # SB_GETTEXTW.
        text = c.create_unicode_buffer(1024)
        assert kernel.ReadProcessMemory(app.handle, app.memory, text, c.sizeof(text), None)
        parts.append(text.value)
    return {"filename": filename.value, "status": parts, "lexer": app.send(4002)}


def missing_dialog(app, desktop):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        found = []

        @CALLBACK
        def visit(window, unused):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(window, c.byref(pid))
            kind = c.create_unicode_buffer(100)
            user.GetClassNameW(window, kind, len(kind))
            if pid.value == app.process.pid and kind.value == "#32770" and user.IsWindowVisible(window):
                found.append(window)
            return True

        user.EnumDesktopWindows(desktop, visit, 0)
        if found:
            assert len(found) == 1
            return found[0]
        time.sleep(0.03)
    raise AssertionError("Owned original missing-file confirmation did not appear")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    portable = args.output / "portable"
    portable.mkdir()
    executable = portable / "Notepad4.exe"
    shutil.copy2(args.executable, executable)
    (portable / "Notepad4.ini").write_text(
        "[Settings]\nSaveSettings=0\nWordWrap=0\nWarnLineEndings=0\nLoadASCIIasUTF8=0\n[Settings2]\nUILanguage=1033\n",
        encoding="utf-16",
    )
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.GetDlgItem.argtypes = [w.HWND, c.c_int]
    user.GetDlgItem.restype = w.HWND
    user.GetDlgItemTextW.argtypes = [w.HWND, c.c_int, w.LPWSTR, c.c_int]
    name = "wxnotepad-startup-file-reference"
    desktop = user.CreateDesktopW(name, None, None, 0, 0x1FF, None)
    assert desktop
    result = {
        "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "sources": [],
        "missing": [],
    }

    def publish():
        (args.output / "startup_files_reference.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )

    try:
        for index, (alias, data) in enumerate(SOURCES):
            target = portable / f"source-{index}.txt"
            target.write_bytes(data)
            app = Original(target, executable, desktop, name, encoding=alias, arguments=("/s", "json"))
            try:
                row = {"alias": alias, "source_hex": data.hex(), **app.state(), **controls(app)}
                app.command(40004)  # Original File/Save, not a fixture encoder.
                row["saved_hex"] = target.read_bytes().hex()
                result["sources"].append(row)
                publish()
                print("source", alias, row["hex"], row["saved_hex"], row["status"], flush=True)
            finally:
                app.close()
        target = portable / "readonly.txt"
        target.write_bytes(b"readonly\r\n")
        mode = target.stat().st_mode
        app = Original(target, executable, desktop, name, arguments=("/ro",))
        try:
            result["readonly"] = {
                **app.state(),
                **controls(app),
                "permissions_unchanged": target.stat().st_mode == mode,
            }
            publish()
        finally:
            app.close()
        for choice, identity in (("yes", 6), ("no", 7), ("cancel", 2)):
            target = portable / (choice + "-missing 中文😀.txt")
            app = Original(target, executable, desktop, name, arguments=("/s", "json", "/ro"))
            try:
                dialog = missing_dialog(app, desktop)
                button = user.GetDlgItem(dialog, identity)
                assert button
                user.SendMessageW(button, 0xF5, 0, 0)  # BM_CLICK on the owned actual button.
                row = {"choice": choice}
                if choice == "cancel":
                    row["exit_code"] = app.process.wait(timeout=10)
                    kernel.CloseHandle(app.handle)
                    app = None
                else:
                    assert user.WaitForInputIdle(app.process.handle, 15000) == 0
                    row.update(app.state())
                    row.update(controls(app))
                row["file_exists"] = target.exists()
                row["file_hex"] = target.read_bytes().hex() if target.exists() else None
                result["missing"].append(row)
                publish()
                print("missing", row, flush=True)
            finally:
                if app is not None:
                    app.close()
    finally:
        user.CloseDesktop(desktop)


if __name__ == "__main__":
    main()
