"""Capture four original Windows quality modes through the owned pinned app.

Run with the shared desktop reserved. Screenshots cover only this owned editor;
the temporary topmost state is used solely to expose its raster for capture.
"""

import argparse
import ctypes as c
import hashlib
import json
import os
import shutil
import sys
from ctypes import wintypes as w
from pathlib import Path
from types import SimpleNamespace

from original_host import Original, user

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from localization_layout import owned_topmost, screenshot_windows

TEXT = (
    "Plain text: English office ffi Café 中文 😀 👩‍💻\r\n"
    "// Comment: English office ffi Café 中文 😀 👩‍💻\r\n"
    "Bold and italic: English office ffi Café 中文 😀 👩‍💻\r\n"
    "Wrapped text: " + "English 中文 Café 😀 " * 14 + "\r\n"
)


def capture(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    portable = output / "portable"
    portable.mkdir()
    source = args.original.resolve()
    executable = portable / "Notepad4.exe"
    shutil.copy2(source, executable)
    (portable / "Notepad4.ini").write_text(
        "[Settings]\nSaveSettings=0\nWordWrap=1\nRenderingTechnology=0\n[Settings2]\nUILanguage=1033\n",
        encoding="utf-16",
    )
    environment = {key: os.environ.get(key) for key in ("__COMPAT_LAYER", "MACTYPE_FORCE_EXCLUDE")}
    if args.dpi96:
        os.environ["__COMPAT_LAYER"] = "DPIUNAWARE"
    os.environ["MACTYPE_FORCE_EXCLUDE"] = "1"
    user.OpenInputDesktop.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    user.OpenInputDesktop.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.GetDpiForWindow.argtypes = [w.HWND]
    user.SetThreadDpiAwarenessContext.argtypes = [w.HANDLE]
    user.SetThreadDpiAwarenessContext.restype = w.HANDLE
    user.RedrawWindow.argtypes = [w.HWND, c.c_void_p, w.HANDLE, w.UINT]
    user.GetMenu.argtypes = [w.HWND]
    user.GetMenu.restype = w.HMENU
    user.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]
    previous = user.SetThreadDpiAwarenessContext(w.HANDLE(-1 if args.dpi96 else -4))
    assert previous
    desktop = user.OpenInputDesktop(0, False, 0x1FF)
    assert desktop
    app = None
    try:
        app = Original(None, executable, desktop, "Default")
        app.reset(TEXT.encode("utf-8"), 0, 0)
        app.send(4001, 0)  # Container styles; the plain-text lexer otherwise resets them during paint.
        app.send(2076, 0)  # No caret blink in controlled raster.
        app.send(2056, 0, app.put(b"Consolas\0"))
        app.send(2061, 0, 1200)
        app.send(2051, 0, 0x141414)
        app.send(2052, 0, 0xFFFFFF)
        app.send(2050)
        app.send(2051, 1, 0x287828)
        app.send(2063, 2, 700)
        app.send(2054, 2, 1)
        start = len(TEXT.splitlines(keepends=True)[0].encode("utf-8"))
        app.send(2032, 0)
        app.send(2033, start, 0)
        app.send(2033, len(TEXT.splitlines(keepends=True)[1].encode("utf-8")), 1)
        app.send(2033, len(TEXT.splitlines(keepends=True)[2].encode("utf-8")), 2)
        app.send(2033, len("".join(TEXT.splitlines(keepends=True)[3:]).encode("utf-8")), 0)
        records = []
        for technology in (0, 1):
            app.command(40046 + technology)
            assert app.send(2631) == technology
            for quality in range(4):
                app.command(40065 + quality)
                assert app.send(2612) == quality
                menu = user.GetMenu(app.hwnd)
                user.SendMessageW(app.hwnd, 0x116, menu, 0)
                assert [bool(user.GetMenuState(menu, 40065 + item, 0) & 8) for item in range(4)] == [
                    item == quality for item in range(4)
                ]
                assert user.RedrawWindow(app.editor, None, None, 0x185)
                assert user.WaitForInputIdle(app.process.handle, 3000) == 0
                styles = [
                    app.send(2038, start),
                    app.send(2038, start + len(TEXT.splitlines(keepends=True)[1].encode("utf-8"))),
                ]
                assert styles == [1, 2], styles
                dpi = user.GetDpiForWindow(app.editor)
                if args.dpi96:
                    assert dpi == 96
                filename = f"original-dpi{dpi}-technology{technology}-quality{quality}.png"
                with owned_topmost(SimpleNamespace(user=user), app.process, app.hwnd):
                    user.SendMessageW(app.hwnd, 0, 0, 0)
                    screenshot_windows(user, app.editor, output / filename, presented=True)
                records.append(
                    {
                        "dpi": dpi,
                        "technology": technology,
                        "quality": quality,
                        "font": "Consolas",
                        "point_size": 12,
                        "image": filename,
                        "painted_styles": styles,
                        "width": app.send(2276, 0, app.put("English 中文 Café 😀\0".encode("utf-8"))),
                        "text_hex": app.text().hex(),
                    }
                )
        (output / "original.json").write_text(
            json.dumps(
                {
                    "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
                    "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                    "capture_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    "dpi96_emulation": args.dpi96,
                    "records": records,
                },
                ensure_ascii=False,
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    finally:
        if app:
            app.close()
        user.CloseDesktop(desktop)
        user.SetThreadDpiAwarenessContext(previous)
        for key, value in environment.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--dpi96", action="store_true")
    capture(parser.parse_args())
