"""Capture pinned original Scheme menus and native Open/Save filter ordering."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import shutil
import time

from original_host import Original, CALLBACK, user, kernel


def capture(original, output, favorites="67 1 32", default=32):
    output.mkdir(parents=True, exist_ok=False)
    portable = output / "portable"
    portable.mkdir()
    executable = portable / "Notepad4.exe"
    shutil.copy2(original, executable)
    (portable / "Notepad4.ini").write_text(
        "[Settings]\nSaveSettings=0\n[Settings2]\nUILanguage=1033\n"
        f"[Styles]\nFavoriteSchemes={favorites}\nDefaultScheme={default}\nAutoSelect=1\n", encoding="utf-16")
    user.GetThreadDesktop.argtypes = [w.DWORD]
    user.GetThreadDesktop.restype = w.HANDLE
    kernel.GetCurrentThreadId.restype = w.DWORD
    desktop = user.GetThreadDesktop(kernel.GetCurrentThreadId())
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.EnumChildWindows.argtypes = [w.HWND, CALLBACK, w.LPARAM]
    user.GetMenu.argtypes = [w.HWND]
    user.GetMenu.restype = w.HMENU
    user.GetSubMenu.argtypes = [w.HMENU, c.c_int]
    user.GetSubMenu.restype = w.HMENU
    user.GetMenuItemCount.argtypes = [w.HMENU]
    user.GetMenuStringW.argtypes = [w.HMENU, w.UINT, w.LPWSTR, c.c_int, w.UINT]
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
    app = Original(None, executable, desktop, "Default")
    records = []
    try:
        scheme_menu = user.GetSubMenu(user.GetMenu(app.hwnd), 4)
        labels = []
        for position in range(user.GetMenuItemCount(scheme_menu)):
            text = c.create_unicode_buffer(256)
            user.GetMenuStringW(scheme_menu, position, text, len(text), 0x400)
            labels.append(text.value)
        for title, command in (("Open", 40001), ("Save", 40005)):
            user.PostMessageW(app.hwnd, 0x111, command, 0)
            deadline = time.monotonic() + 15
            found = []
            while time.monotonic() < deadline:
                found.clear()
                @CALLBACK
                def visit(hwnd, _):
                    pid = w.DWORD()
                    user.GetWindowThreadProcessId(hwnd, c.byref(pid))
                    kind = c.create_unicode_buffer(128)
                    user.GetClassNameW(hwnd, kind, len(kind))
                    if pid.value == app.process.pid and kind.value == "#32770" and user.IsWindowVisible(hwnd):
                        found.append(hwnd)
                    return True
                user.EnumDesktopWindows(desktop, visit, 0)
                if found:
                    break
                time.sleep(.02)
            assert len(found) == 1, (title, "owned native file dialog appeared")
            dialog = found[0]
            filters = []
            @CALLBACK
            def child(hwnd, _):
                kind = c.create_unicode_buffer(128)
                user.GetClassNameW(hwnd, kind, len(kind))
                if kind.value == "ComboBox" and user.IsWindowVisible(hwnd):
                    count = user.SendMessageW(hwnd, 0x146, 0, 0)
                    entries = []
                    for index in range(count):
                        size = user.SendMessageW(hwnd, 0x149, index, 0)
                        assert 0 <= size < 32768
                        value = c.create_unicode_buffer(size + 1)
                        user.SendMessageW(hwnd, 0x148, index, c.addressof(value))
                        entries.append(value.value)
                    if entries and any(" (*" in value for value in entries):
                        filters.append({"selected": user.SendMessageW(hwnd, 0x147, 0, 0), "entries": entries})
                return True
            # Modern file dialogs populate their native filter control asynchronously.
            while time.monotonic() < deadline and not filters:
                user.EnumChildWindows(dialog, child, 0)
                if not filters:
                    time.sleep(.03)
            assert len(filters) == 1, (title, filters)
            assert user.SetWindowPos(dialog, w.HWND(-1), 0, 0, 0, 0, 0x13)
            time.sleep(.1)
            rect = w.RECT()
            assert user.GetWindowRect(dialog, c.byref(rect))
            from PIL import ImageGrab
            ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom)).save(output / f"{title.lower()}.png")
            assert user.SetWindowPos(dialog, w.HWND(-2), 0, 0, 0, 0, 0x13)
            records.append({"dialog": title, "filter": filters[0], "document": app.state()})
            user.SendMessageW(dialog, 0x111, 2, 0)
            user.SendMessageW(app.hwnd, 0, 0, 0)
        assert records[0]["filter"]["entries"][0].startswith("All Files"), records
        assert records[1]["filter"]["entries"][-1].startswith("All Files"), records
        assert all("TOML" not in entry for row in records for entry in row["filter"]["entries"]), records
        (output / "observations.json").write_text(json.dumps({
            "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
            "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "favoriteSchemes": favorites, "defaultScheme": default,
            "schemeMenu": labels, "records": records}, indent=2) + "\n", encoding="utf-8")
        print("PASS: independent native original fixed Scheme menu and current/text/favorite file filters")
    finally:
        app.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--favorites", default="67 1 32")
    parser.add_argument("--default", type=int, default=32)
    args = parser.parse_args()
    capture(args.original.resolve(), args.output.resolve(), args.favorites, args.default)
