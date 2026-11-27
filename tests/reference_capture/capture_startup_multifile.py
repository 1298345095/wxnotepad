"""Capture original multi-file parsing and dispatch on an owned private desktop."""

import argparse
import ctypes as c
import hashlib
import json
import shutil
import time
from ctypes import wintypes as w
from pathlib import Path

from capture_startup_files import controls
from original_host import CALLBACK, Original, kernel, user
from private_desktop import Process


def windows(desktop):
    found = []

    @CALLBACK
    def visit(hwnd, unused):
        name = c.create_unicode_buffer(100)
        user.GetClassNameW(hwnd, name, len(name))
        if name.value == "Notepad4":
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            editor = user.FindWindowExW(hwnd, None, "Scintilla", None)
            if editor:
                found.append((hwnd, editor, pid.value))
        return True

    user.EnumDesktopWindows(desktop, visit, 0)
    return found


def attach(hwnd, editor, pid):
    app = Original.__new__(Original)
    app.hwnd, app.editor = hwnd, editor
    app.process = Process.__new__(Process)
    app.process.pid = pid
    app.process.handle = kernel.OpenProcess(0x100000 | 0x1000, False, pid)
    assert app.process.handle
    assert user.WaitForInputIdle(app.process.handle, 15000) == 0
    app.handle = kernel.OpenProcess(0x38, False, pid)
    assert app.handle
    app.memory = kernel.VirtualAllocEx(app.handle, None, 1048576, 0x3000, 4)
    assert app.memory
    return app


def settle(app, desktop, cancel=False):
    """Finish an owned startup prompt before inspecting or closing its editor."""
    prompts = []
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        dialogs = []

        @CALLBACK
        def visit(hwnd, unused):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(100)
            user.GetClassNameW(hwnd, name, len(name))
            if pid.value == app.process.pid and name.value == "#32770" and user.IsWindowVisible(hwnd):
                dialogs.append(hwnd)
            return True

        user.EnumDesktopWindows(desktop, visit, 0)
        for dialog in dialogs:
            text = []

            @CALLBACK
            def child(hwnd, unused):
                value = c.create_unicode_buffer(32768)
                user.GetWindowTextW(hwnd, value, len(value))
                if value.value:
                    text.append(value.value)
                return True

            user.EnumChildWindows(dialog, child, 0)
            prompts.append(text)
            button = user.GetDlgItem(dialog, 2) if cancel else user.GetDlgItem(dialog, 7) or user.GetDlgItem(dialog, 1)
            assert button, text
            user.SendMessageW(button, 0xF5, 0, 0)
        if len(controls(app)["status"]) > 1 and not dialogs:
            return prompts
        time.sleep(0.03)
    raise AssertionError("Original startup did not settle after owned prompt")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", action="append", default=[])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    portable = args.output / "portable"
    portable.mkdir()
    executable = portable / "Notepad4.exe"
    shutil.copy2(args.executable, executable)
    original_hash = "1425c9a4b5ab2ea1fa3ba088f38dd987b763b7b8feff23dde6243f7855e86a78"
    assert hashlib.sha256(executable.read_bytes()).hexdigest() == original_hash
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.FindWindowExW.argtypes = [w.HWND, w.HWND, w.LPCWSTR, w.LPCWSTR]
    user.FindWindowExW.restype = w.HWND
    user.GetDlgItem.argtypes = [w.HWND, c.c_int]
    user.GetDlgItem.restype = w.HWND
    user.GetDlgItemTextW.argtypes = [w.HWND, c.c_int, w.LPWSTR, c.c_int]
    user.GetMenu.argtypes = [w.HWND]
    user.GetMenu.restype = w.HMENU
    user.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]
    name = "wxnotepad-multifile-reference-287"
    desktop = user.CreateDesktopW(name, None, None, 0, 0x1FF, None)
    assert desktop
    result = {"revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a", "executable_sha256": original_hash, "cases": []}
    targets = [portable / f"file {index} 中文😀.txt" for index in range(35)]
    for index, path in enumerate(targets):
        path.write_bytes(f"first {index}\r\ncaf\xe9\r\n".encode("latin-1"))
    (portable / "-switch.txt").write_bytes(b"switch\r\n")
    (portable / "directory 中文😀").mkdir()
    try:
        for label, default, options, filenames, expected in (
            ("zero", False, ["+"], [], 1),
            ("one", False, ["+"], targets[:1], 1),
            ("two", False, ["+"], targets[:2], 2),
            ("32", False, ["+"], targets[:32], 32),
            ("35", False, ["+"], targets[:35], 32),
            # Pinned startup parses before LoadFlags: this setting arrives too
            # late for collection. Retain the observed defect independently.
            ("default-two", True, [], targets[:2], 1),
            ("default-one-known-flags", True, [], targets[:1], 1),
            ("default-forced-single", True, ["-"], [targets[0]], 1),
            ("switch-name", True, ["-"], [portable / "-switch.txt"], 1),
            ("relative", False, ["+"], [targets[0].name, targets[1]], 2),
            ("single-tail", True, ["-"], targets[0].name.split(" "), 1),
            ("duplicate", False, ["+"], [targets[0], targets[0]], 2),
            ("directory-cancel", False, ["+"], [targets[0], portable / "directory 中文😀"], 2),
            ("options", False, ["/rs", "/e", "windows-1252", "/s", "json", "/ro", "/g", "2,2", "+"], targets[:2], 2),
        ):
            if args.case and label not in args.case:
                continue
            config = portable / "Notepad4.ini"
            config.write_text(
                f"[Settings]\nSaveSettings=0\nWarnLineEndings=0\nWordWrap=0\n"
                f"[Settings2]\nUILanguage=1033\nMultiFileArg={int(default)}\nReuseWindow=1\nSingleFileInstance=1\n"
                + ("ReadOnlyMode=1\n" if label in ("default-two", "default-one-known-flags") else ""),
                encoding="utf-16",
            )
            before = config.read_bytes()
            parent = Process([executable, *options, *filenames], name, cwd=portable)
            apps = []
            try:
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    found = windows(desktop)
                    if len(found) == expected:
                        break
                    time.sleep(0.03)
                apps = [attach(*entry) for entry in found]
                assert len(found) == expected, (label, found)
                row = {"name": label, "default": default, "options": options, "input_count": len(filenames)}
                row["prompts"] = [settle(app, desktop, cancel=label == "directory-cancel") for app in apps]
                row["documents"] = [
                    {
                        "parent": app.process.pid == parent.pid,
                        **app.state(),
                        **controls(app),
                        "reuse_checked": bool(user.GetMenuState(user.GetMenu(app.hwnd), 40423, 0) & 8),
                        "single_checked": bool(user.GetMenuState(user.GetMenu(app.hwnd), 40040, 0) & 8),
                    }
                    for app in apps
                ]
                if expected > 1:
                    row["parent_exit"] = parent.wait(timeout=5)
                    parent = None
                result["cases"].append(row)
                print(label, len(apps), [app.state()["caret"] for app in apps], flush=True)
            finally:
                for app in apps:
                    app.close()
                if parent is not None:
                    row["parent_exit"] = parent.wait(timeout=5)
            assert config.read_bytes() == before
            (args.output / "startup_multifile_reference.json").write_text(
                json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
            )
    finally:
        user.CloseDesktop(desktop)


if __name__ == "__main__":
    main()
