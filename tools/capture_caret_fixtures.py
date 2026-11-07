"""Query the pinned original's caret commands and portable settings, without desktop input."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def capture(executable, output):
    user = c.WinDLL("user32", use_last_error=True)
    send = user.SendMessageW
    send.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
    send.restype = c.c_ssize_t
    user.PostMessageW.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
    user.GetMenu.argtypes = [w.HWND]
    user.GetMenu.restype = w.HMENU
    user.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]
    user.GetDpiForWindow.argtypes = [w.HWND]
    user.GetCaretBlinkTime.restype = w.UINT
    user.GetDC.argtypes = [w.HWND]
    user.GetDC.restype = w.HDC
    user.ReleaseDC.argtypes = [w.HWND, w.HDC]
    user.RedrawWindow.argtypes = [w.HWND, c.c_void_p, w.HANDLE, w.UINT]
    gdi = c.WinDLL("gdi32", use_last_error=True)
    gdi.GetPixel.argtypes = [w.HDC, c.c_int, c.c_int]
    gdi.GetPixel.restype = w.DWORD
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.VirtualAllocEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD]
    kernel.VirtualAllocEx.restype = c.c_void_p
    kernel.WriteProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
    kernel.VirtualFreeEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, c.c_ssize_t)
    result = {"revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a", "commands": [], "settings": []}
    system_width = w.DWORD()
    assert user.SystemParametersInfoW(0x2006, 0, c.byref(system_width), 0)
    result["system_width"] = system_width.value
    result["system_blink_period"] = user.GetCaretBlinkTime()
    command_ids = {"Block (OVR Mode)": 40069, "Block (INS Mode)": 40070, "Line Width 1": 40071,
                   "Line Width 2": 40072, "Line Width 3": 40073, "No Blink": 40074,
                   "Draw Block Caret in Selection": 40075}

    def save_now(frame):
        pid = w.DWORD()
        user.GetWindowThreadProcessId(frame, c.byref(pid))
        dialogs = []

        @callback
        def dialog(hwnd, _):
            owner = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(owner))
            name = c.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, 128)
            if owner.value == pid.value and name.value == "#32770":
                dialogs.append(hwnd)
            return True

        assert user.PostMessageW(frame, 0x111, 40444, 0)
        deadline = time.monotonic() + 10
        while not dialogs and time.monotonic() < deadline:
            user.EnumWindows(dialog, 0)
            time.sleep(.05)
        assert len(dialogs) == 1, "original Save Settings Now confirmation must appear"
        send(dialogs[0], 0x111, 1, 0)

    def run(folder, ini, inspect):
        path = folder / executable.name
        if not path.exists():
            shutil.copy2(executable, path)
        (folder / "Notepad4.ini").write_text("[Settings]\n" + ini + "\n", encoding="utf-16")
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        process = subprocess.Popen([str(path), "/ns", "/n"], startupinfo=startup)
        windows = []

        @callback
        def top(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, 128)
            if pid.value == process.pid and name.value == "Notepad4":
                windows.append(hwnd)
            return True

        deadline = time.monotonic() + 10
        while not windows and time.monotonic() < deadline:
            user.EnumWindows(top, 0)
            time.sleep(.05)
        if len(windows) != 1:
            process.terminate()
            raise RuntimeError(f"owned original window missing: {windows}")
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
        assert len(children) == 1, children
        editor = children[0]

        def state():
            # Original InitMenu checks are populated through WM_INITMENU.
            menu = user.GetMenu(frame)
            send(frame, 0x116, menu, 0)
            return {"dpi": user.GetDpiForWindow(editor),
                    "style": send(editor, 2513, 0, 0), "width": send(editor, 2189, 0, 0),
                    "period": send(editor, 2075, 0, 0), "zoom": send(editor, 2374, 0, 0),
                    "checks": {name: bool(user.GetMenuState(menu, number, 0) & 8)
                               for name, number in command_ids.items()}}

        try:
            inspect(frame, editor, state)
            if not ini:
                handle = kernel.OpenProcess(0x38, False, process.pid)
                memory = kernel.VirtualAllocEx(handle, None, 4096, 0x3000, 4)
                try:
                    text = "Wi中😀e\u0301 \r\n".encode()
                    assert kernel.WriteProcessMemory(handle, memory, text + b"\0", len(text) + 1, None)
                    send(editor, 2037, 65001, 0)
                    send(editor, 2181, 0, memory)
                    send(editor, 2373, 100, 0)
                    send(editor, 2076, 0, 0)
                    send(editor, 2630, 0, 0)
                    result["paint_technology"] = "original GDI public rendering mode"
                    user.ShowWindow(frame, 5)
                    send(editor, 2380, 1, 0)

                    def pixels(style, y):
                        send(editor, 2512, style, 0)
                        user.RedrawWindow(editor, None, None, 0x185)
                        dc = user.GetDC(editor)
                        try:
                            return [gdi.GetPixel(dc, x, y) for x in range(300)]
                        finally:
                            user.ReleaseDC(editor, dc)

                    result["paint"] = []
                    for anchor, caret in [(0, 0), (1, 1), (2, 2), (5, 5), (9, 9), (10, 10), (12, 12),
                                          (0, 5), (5, 0), (0, len(text)), (len(text), len(text))]:
                        send(editor, 2160, anchor, caret)
                        for style in [2, 258, 1, 17]:
                            for overstrike in [0, 1]:
                                send(editor, 2186, overstrike, 0)
                                y = send(editor, 2165, 0, caret) + 2
                                background = pixels(0, y)
                                foreground = pixels(style, y)
                                changed = [x for x, (a, b) in enumerate(zip(background, foreground)) if a != b]
                                result["paint"].append({"anchor": anchor, "caret": caret, "style": style,
                                    "overstrike": overstrike, "caret_x": send(editor, 2164, 0, caret),
                                    "caret_y": send(editor, 2165, 0, caret),
                                    "changed": [min(changed), max(changed) + 1] if changed else []})
                    assert result["paint"][0]["changed"], "original native block paint must produce observable pixels"
                finally:
                    kernel.VirtualFreeEx(handle, memory, 0, 0x8000)
                    kernel.CloseHandle(handle)
        finally:
            send(editor, 2014, 0, 0)
            send(frame, 0x10, 0, 0)
            process.wait(timeout=10)
        return (folder / "Notepad4.ini").read_text(encoding="utf-16")

    with tempfile.TemporaryDirectory(prefix="notepad4-caret-reference-") as temporary:
        folder = Path(temporary)

        def commands(frame, editor, state):
            result["default"] = state()
            for name in ["No Blink", "Block (INS Mode)", "Line Width 1", "Line Width 2", "Line Width 3",
                         "Block (OVR Mode)", "Block (INS Mode)", "Draw Block Caret in Selection", "No Blink"]:
                send(frame, 0x111, command_ids[name], 0)
                result["commands"].append({"command": name, **state()})
            for width, number in [(1, 40071), (2, 40072), (3, 40073)]:
                send(frame, 0x111, number, 0)
                for zoom in [50, 100, 150, 200, 500]:
                    send(editor, 2373, zoom, 0)
                    # The menu path reapplies Style_UpdateCaret after zoom.
                    send(frame, 0x111, number, 0)
                    result.setdefault("zoom", []).append({"requested_width": width, **state()})
        run(folder, "", commands)
        for style, period in [(1, -1), (0, 0), (113, 275), (999, -7), (-1, -1), (104, 0),
                              (2147483647, 2147483647), (-2147483648, -2147483648)]:
            row = {"input_style": style, "input_period": period}

            def inspect(frame, editor, state):
                row["initial"] = state()
                save_now(frame)
                row["saved_before_toggle"] = [line for line in (folder / "Notepad4.ini").read_text(encoding="utf-16").splitlines()
                                              if line.startswith(("CaretStyle=", "CaretBlinkPeriod="))]
                send(frame, 0x111, 40074, 0)
                row["toggled"] = state()
            saved = run(folder, f"CaretStyle={style}\nCaretBlinkPeriod={period}", inspect)
            row["saved"] = [line for line in saved.splitlines() if line.startswith(("CaretStyle=", "CaretBlinkPeriod="))]
            result["settings"].append(row)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    fixture = Path(__file__).resolve().parents[1] / "tests/caret_reference.tsv"
    with fixture.open("w", encoding="utf-8", newline="") as target:
        target.write("# Original Notepad4 89b8b36, captured by tools/capture_caret_fixtures.py\n")
        target.write("# input packed style, input blink period, queried core style, canonical packed style\n")
        for row in result["settings"]:
            saved = next((line.split("=", 1)[1] for line in row["saved"] if line.startswith("CaretStyle=")), "1")
            target.write(f'{row["input_style"]}\t{row["input_period"]}\t{row["initial"]["style"]}\t{saved}\n')
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    capture(args.executable.resolve(), args.output.resolve())
