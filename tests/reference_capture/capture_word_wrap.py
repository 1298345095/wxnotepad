"""Capture actual pinned-original wrap dialog results on an owned private desktop.

No clipboard or desktop input is used. Normal mode uses the monitor's real DPI;
--dpi96 uses the established complementary DPI-unaware process emulation.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import shutil
import time

from original_host import Original, CALLBACK, user, kernel

# Pinned Notepad4 changes STYLE_DEFAULT from upstream Scintilla's 32 to zero.
STYLE_DEFAULT = 0
FONT_SIZE_HUNDREDTHS = 1000

def capture(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    portable = output / "portable"
    portable.mkdir()
    original = args.original.resolve()
    executable = portable / "Notepad4.exe"
    shutil.copy2(original, executable)
    (portable / "Notepad4.ini").write_text(
        "[Settings]\nSaveSettings=0\nWordWrap=1\nWordWrapMode=4\nTabWidth=8\nIndentWidth=3\nDetectIndentation=0\n"
        "[Settings2]\nUILanguage=1033\n",
        encoding="utf-16")
    if args.dpi96:
        os.environ["__COMPAT_LAYER"] = "DPIUNAWARE"
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.GetDlgItem.argtypes = [w.HWND, c.c_int]
    user.GetDlgItem.restype = w.HWND
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.EnumChildWindows.argtypes = [w.HWND, CALLBACK, w.LPARAM]
    user.GetDpiForWindow.argtypes = [w.HWND]
    user.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.MoveWindow.argtypes = [w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.BOOL]
    user.RedrawWindow.argtypes = [w.HWND, c.c_void_p, w.HANDLE, w.UINT]
    user.WaitForInputIdle.argtypes = [w.HANDLE, w.DWORD]
    user.SetThreadDpiAwarenessContext.argtypes = [w.HANDLE]
    user.SetThreadDpiAwarenessContext.restype = w.HANDLE
    # Cross-process geometry calls use the caller's DPI context. Match it to
    # the target rather than letting Python's default virtualization resize a
    # 144-DPI editor to 720 physical pixels for a requested 480-pixel viewport.
    previous_dpi = user.SetThreadDpiAwarenessContext(w.HANDLE(-1 if args.dpi96 else -4))
    assert previous_dpi, "capture thread uses the intended target DPI context"
    desktop_name = f"wxnotepad-wrap-reference-{os.getpid()}"
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x1ff, None)
    assert desktop, "owned private desktop is created"
    app = None
    records = []
    try:
        app = Original(None, executable, desktop, desktop_name)
        assert user.WaitForInputIdle(app.process.handle, 15000) == 0, "original startup completes"
        user.SendMessageW(app.hwnd, 0, 0, 0)  # Owning GUI thread barrier after input idle.
        actual_dpi = user.GetDpiForWindow(app.editor)
        if args.dpi96:
            assert actual_dpi == 96, actual_dpi

        def paint():
            assert user.WaitForInputIdle(app.process.handle, 3000) == 0
            assert user.RedrawWindow(app.editor, None, None, 0x101)  # RDW_INVALIDATE | RDW_UPDATENOW
            user.SendMessageW(app.hwnd, 0, 0, 0)

        def dialog(mode, indent, highlight=True):
            core_before = app.send(2773)
            mode_before = app.send(2269)
            assert user.PostMessageW(app.hwnd, 0x111, 40419, 0)
            found = []
            deadline = time.monotonic() + 3
            while not found and time.monotonic() < deadline:
                @CALLBACK
                def window(hwnd, _):
                    pid = w.DWORD()
                    user.GetWindowThreadProcessId(hwnd, c.byref(pid))
                    name = c.create_unicode_buffer(100)
                    user.GetClassNameW(hwnd, name, 100)
                    if (pid.value == app.process.pid and name.value == "#32770" and user.IsWindowVisible(hwnd)
                            and all(user.GetDlgItem(hwnd, identifier) for identifier in range(100, 106))):
                        found.append(hwnd)
                    return True
                user.EnumDesktopWindows(desktop, window, 0)
                if not found:
                    time.sleep(.01)
            assert len(found) == 1, "owned original wrap dialog appears"
            hwnd = found[0]
            assert user.WaitForInputIdle(app.process.handle, 3000) == 0
            checkbox_before = user.SendMessageW(user.GetDlgItem(hwnd, 105), 0xf0, 0, 0)
            # Original resource.h IDC_WRAP_INDENT..IDC_WRAP_HIGHLIGHT_SUBLINE.
            for identifier, value in ((100, indent), (101, 1), (102, 3), (103, mode)):
                control = user.GetDlgItem(hwnd, identifier)
                assert control
                user.SendMessageW(control, 0x14e, value, 0)  # CB_SETCURSEL
                assert user.SendMessageW(control, 0x147, 0, 0) == value  # CB_GETCURSEL
            user.SendMessageW(user.GetDlgItem(hwnd, 104), 0xf1, 1, 0)  # BM_SETCHECK
            # Retain the original's active highlight state. Its dialog updates
            # the global preference but does not send the core highlight message.
            user.SendMessageW(user.GetDlgItem(hwnd, 105), 0xf1, int(highlight), 0)
            checkbox_selected = user.SendMessageW(user.GetDlgItem(hwnd, 105), 0xf0, 0, 0)
            user.SendMessageW(hwnd, 0x111, 1, 0)
            paint()
            return {"core_before": core_before, "checkbox_before": checkbox_before,
                    "checkbox_selected": checkbox_selected, "core_after": app.send(2773),
                    "wrap_mode_before": mode_before, "wrap_mode_after": app.send(2269),
                    "requested_mode": mode, "parent_command_barrier": "WaitForInputIdle + WM_NULL + native paint"}

        highlight_runtime = [dialog(2, 2, False), dialog(3, 3, True)]
        (output / "highlight-runtime.json").write_text(json.dumps(highlight_runtime, indent=2) + "\n",
                                                        encoding="utf-8")
        print("Original active-highlight runtime:", json.dumps(highlight_runtime), flush=True)

        for sample, fixture_hex in (line.split() for line in args.inputs.read_text(encoding="ascii").splitlines()):
            text = bytes.fromhex(fixture_hex)
            for mode in (1, 2, 3, 4):
                indent = (mode - 1) + (4 if sample == "folded" else 0)
                for width in (480, 720):
                    app.reset(text, 6, 1)
                    assert app.text() == text, "intended original buffer survives startup and reset"
                    app.send(2037, 65001)  # UTF-8
                    app.send(4001, 1)  # original Null lexer; fold scene below is explicit.
                    app.send(2031, 0)
                    app.send(2630, 0)  # original GDI renderer, independent wx GDI comparison.
                    app.send(2373, 100)
                    app.send(2611, 0)  # default font quality
                    app.send(2760, 0, app.put(b"\0"))  # explicit empty font locale
                    app.send(2056, STYLE_DEFAULT, app.put(b"Consolas\0"))
                    app.send(2061, STYLE_DEFAULT, FONT_SIZE_HUNDREDTHS)
                    app.send(2063, STYLE_DEFAULT, 400)
                    app.send(2054, STYLE_DEFAULT, 0)
                    app.send(2066, STYLE_DEFAULT, 1)  # SC_CHARSET_DEFAULT
                    app.send(2258, STYLE_DEFAULT, 5)  # normal font stretch
                    app.send(2050)
                    font_length = app.send(2486, STYLE_DEFAULT)
                    app.send(2486, STYLE_DEFAULT, app.memory)
                    font = c.create_string_buffer(font_length + 1)
                    assert kernel.ReadProcessMemory(app.handle, app.memory, font, font_length + 1, None)
                    attributes = (font.value.decode("utf-8"), app.send(2062, STYLE_DEFAULT), app.send(2374),
                                  app.send(2631))
                    if not records:
                        print("original font attributes", attributes, flush=True)
                    assert attributes[:4] == ("Consolas", FONT_SIZE_HUNDREDTHS, 100, 0), attributes
                    app.send(2036, 8)
                    app.send(2122, 3)
                    app.send(2525, 0)
                    app.send(2527, 0)
                    app.send(2155, 0, 1)
                    app.send(2157, 0, 1)
                    for margin in range(5):
                        app.send(2242, margin, 0)
                    app.send(2282, 1, app.put(b"!"))
                    app.send(2176)
                    app.send(2160, 6, 1)
                    if sample == "folded":
                        app.send(2222, 0, 0x2400)
                        for line in range(1, 4):
                            app.send(2222, line, 0x401)
                        app.send(2222, 4, 0x400)
                        app.send(2237, 0, 0)
                    dialog(mode, indent)
                    for margin in range(5):
                        app.send(2242, margin, 0)
                    # Normalize the *actual client* viewport, excluding native scrollbars.
                    for _ in range(3):
                        client, outer = w.RECT(), w.RECT()
                        user.GetClientRect(app.editor, c.byref(client))
                        user.GetWindowRect(app.editor, c.byref(outer))
                        assert user.MoveWindow(app.editor, 0, 0,
                                               outer.right - outer.left + width - client.right,
                                               outer.bottom - outer.top + 340 - client.bottom, True)
                        paint()
                        user.GetClientRect(app.editor, c.byref(client))
                        if (client.right, client.bottom) == (width, 340):
                            break
                    assert (client.right, client.bottom) == (width, 340), (client.right, client.bottom)
                    assert app.text() == text
                    values = [app.send(message) for message in
                              (2269, 2473, 2465, 2461, 2463, 2557, 2773)]
                    values += [app.send(2235, 0), app.send(2228, 2), app.send(2009), app.send(2008),
                               app.send(2159), app.send(2174), app.send(2016), app.send(2279, 0),
                               app.send(2276, STYLE_DEFAULT, app.put(b"W\0")),
                               app.send(2276, STYLE_DEFAULT, app.put("中文\0".encode("utf-8")))]
                    records.append([sample, mode, indent, width, 340, actual_dpi, *values])
                    print(sample, mode, indent, width, actual_dpi, *values, flush=True)
        header = "sample mode indent width height dpi wrap indent_mode start_indent flags location margin subline rows visible2 anchor caret dirty undo redo line_height W CJK"
        (output / "reference.tsv").write_text(header + "\n" +
            "\n".join("\t".join(map(str, row)) for row in records) + "\n", encoding="ascii")
        (output / "metadata.json").write_text(json.dumps({
            "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
            "executable": str(original), "sha256": hashlib.sha256(original.read_bytes()).hexdigest(),
            "dpi": actual_dpi, "dpi96_emulation": args.dpi96, "records": len(records),
            "capture_thread_context": "DPI_UNAWARE" if args.dpi96 else "PER_MONITOR_AWARE_V2",
            "renderer": "GDI", "font": "Consolas", "point_size": 10, "reported_client_height": 340,
            "tab_width": 8, "indent_width": 3, "highlight_subline_retained": True,
            "inputs_sha256": hashlib.sha256(args.inputs.read_bytes()).hexdigest(),
            "assumptions": "All styles plain Consolas, 1px edge margins, no margin columns; fold levels explicit. "
                           "Client coordinates are native reported pixels, virtualized in the 96 DPI emulation. "
                           "No pixel identity or physical monitor-transition claim."
        }, indent=2) + "\n", encoding="utf-8")
        print(f"PASS: {len(records)} independent original native-dialog wrap records at {actual_dpi} DPI")
    finally:
        if app:
            app.close()
        user.CloseDesktop(desktop)
        user.SetThreadDpiAwarenessContext(previous_dpi)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("inputs", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--dpi96", action="store_true")
    capture(parser.parse_args())
