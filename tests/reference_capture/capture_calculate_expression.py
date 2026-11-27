"""Capture Calculate Expression from a pinned independent original application.

Usage: python capture_calculate_expression.py PINNED_NOTEPAD4_EXE EXTERNAL_OUTPUT
No clipboard or foreground input is used. The caller owns the Windows GUI slot.
"""

import ctypes as c
import hashlib
import json
import os
import shutil
import sys
import threading
import time
from ctypes import wintypes as w
from pathlib import Path

from original_host import CALLBACK, Original, user

# Pinned Notepad4/Scintilla and native platform protocol values.
DESKTOP_OBJECT_ACCESS = 0x1FF
IDM_ENCODING_ANSI = 40100
IDM_FILE_SAVE = 40004
SCI_ADDSELECTION = 2573
SCI_GETCODEPAGE = 2137
SCI_REDO = 2011
SCI_SETMAINSELECTION = 2574
SCI_SETREADONLY = 2171
SCI_SETSEL = 2160
SCI_SETSELECTIONMODE = 2422
SCI_SETSELECTIONNANCHOR = 2578
SCI_SETSELECTIONNCARETVIRTUALSPACE = 2580
SCI_UNDO = 2176
WM_CLOSE = 0x10
WM_INITMENU = 0x116

CALCULATE = 40588
exe, output = map(Path, sys.argv[1:3])
output.mkdir(parents=True, exist_ok=False)
portable = output / "portable"
portable.mkdir()
shutil.copy2(exe, portable / "Notepad4.exe")
(portable / "Notepad4.ini").write_text(
    "[Settings]\nSaveSettings=0\nWordWrap=0\nWarnLineEndings=0\n[Settings2]\nUILanguage=1033\n", encoding="utf-16"
)
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
user.GetMenu.argtypes = [w.HWND]
user.GetMenu.restype = w.HMENU
user.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]
name = "wxnotepad-calculate-reference-" + str(os.getpid())
desktop = user.CreateDesktopW(name, None, None, 0, DESKTOP_OBJECT_ACCESS, None)
assert desktop
rows = []


def invoke(app, rectangle=False):
    if not rectangle:
        app.command(CALCULATE)
        return []
    worker = threading.Thread(target=app.command, args=(CALCULATE,))
    worker.start()
    dialogs = []
    deadline = time.monotonic() + 5
    while worker.is_alive() and time.monotonic() < deadline:

        def window(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            kind = c.create_unicode_buffer(100)
            user.GetClassNameW(hwnd, kind, 100)
            if pid.value == app.process.pid and kind.value == "#32770" and hwnd not in dialogs:
                dialogs.append(hwnd)
                user.PostMessageW(hwnd, WM_CLOSE, 0, 0)
            return True

        user.EnumDesktopWindows(desktop, CALLBACK(window), 0)
        time.sleep(0.01)
    worker.join(timeout=1)
    assert not worker.is_alive(), "owned rectangular-selection warning completed"
    return dialogs


try:
    document = output / "document.txt"
    document.write_bytes(b"")
    app = Original(document, portable / "Notepad4.exe", desktop, name)
    try:
        expressions = [
            "1+2*3",
            "(1+2)*3",
            "2^3",
            "0xff + 0b10 + 0o10",
            "1e3 + .5",
            "-7%3",
            "1<<4|3",
            "sqrt(81)",
            "PI",
            "sin(PI/2)",
            "pow(2,10)",
            "0.1+0.2",
            "1/3",
            "1e20",
            "1e21",
            "NaN",
            "Infinity",
            "-Infinity",
            "-0",
            "true",
            "false",
            "null",
            "undefined",
            '""',
            '"a"+3',
            "[1,2,3]",
            "({a:1})",
            '({toString:function(){return "hello";}})',
            "var x=3;x",
            "typeof x",
            "(()=>42)()",
            "let x=4;x",
            "1+",
            "foo",
            "sqrt()",
            "2**3",
            "new Date(0)",
            "077",
            "new Map([[1,2]]).get(1)",
            '"中文😀"',
            '"a\\nb"',
            "1 +\n2",
            'throw new Error("owned error")',
        ]
        if "--supplement" in sys.argv[3:]:
            expressions = [
                r'"a\u0000b"',
                r'"\u0000"',
                "throw null",
                "throw undefined",
                "throw 7",
                '"中文";foo',
                '"😀";foo',
                "1+\nfoo",
                "1+\rfoo",
                "1+\u2028foo",
                "1+" * 550 + "1",
            ]
        for expression in expressions:
            for reverse in [False, True]:
                data = ("pre " + expression + " tail").encode()
                end = 4 + len(expression.encode())
                app.reset(data, end if reverse else 4, 4 if reverse else end)
                before = app.state()
                invoke(app)
                after = app.state()
                app.command(IDM_FILE_SAVE)  # Save, not direct file reconstruction.
                saved = document.read_bytes().hex()
                app.send(SCI_UNDO)
                undo = app.state()
                app.send(SCI_REDO)
                redo = app.state()
                rows.append(
                    dict(
                        expression=expression,
                        reverse=reverse,
                        before=before,
                        after=after,
                        saved=saved,
                        undo=undo,
                        redo=redo,
                    )
                )
                (output / "partial.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
                print(expression, reverse, bytes.fromhex(after["hex"]).decode(errors="replace"), flush=True)
        modes = ["empty", "readonly", "rectangle", "multiple"]
        if "--supplement" in sys.argv[3:]:
            modes.append("multiple-main-first")
        for mode in modes:
            app.reset(b"1+2\n3+4", 0, 0 if mode == "empty" else 3)
            if mode == "readonly":
                app.send(SCI_SETREADONLY, 1)
            if mode == "rectangle":
                app.send(SCI_SETSELECTIONMODE, 1)
                app.send(SCI_SETSELECTIONNCARETVIRTUALSPACE, 0)
                app.send(SCI_SETSELECTIONNANCHOR, 7)
            if mode in ("multiple", "multiple-main-first"):
                app.send(SCI_ADDSELECTION, 7, 4)
                if mode == "multiple-main-first":
                    app.send(SCI_SETMAINSELECTION, 0)
            before = app.state()
            menu = user.GetMenu(app.hwnd)
            user.SendMessageW(app.hwnd, WM_INITMENU, menu, 0)
            enabled = not (user.GetMenuState(menu, CALCULATE, 0) & 3)
            dialogs = invoke(app, mode == "rectangle")
            after = app.state()
            app.send(SCI_UNDO)
            undo = app.state()
            app.send(SCI_REDO)
            rows.append(
                dict(
                    mode=mode,
                    before=before,
                    enabled=enabled,
                    after=after,
                    undo=undo,
                    redo=app.state(),
                    dialogs=len(dialogs),
                )
            )
    finally:
        app.close()
    if "--supplement" in sys.argv[3:]:
        encodings = [
            ("utf-8", "utf-8", bytes.fromhex("efbbbf"), "中"),
            ("utf-16le", "utf-16le", bytes.fromhex("fffe"), "中"),
            ("utf-16be", "utf-16be", bytes.fromhex("feff"), "中"),
            ("gbk", "gbk", b"", "中"),
            ("gb18030", "gb18030", b"", "😀"),
            ("big5", "big5", b"", "中"),
            ("windows-1252", "cp1252", b"", "é"),
            ("cp437", "cp437", b"", "é"),
            ("cp932", "cp932", b"", "あ"),
            ("cp949", "cp949", b"", "한"),
            ("ansi", "ascii", b"", "😀"),
        ]
        for encoding, codec, bom, value in encodings:
            expression = json.dumps(value, ensure_ascii=True)
            content = "pre " + expression + " tail\r\n"
            target = output / (encoding + ".txt")
            target.write_bytes(b"" if encoding == "ansi" else bom + content.encode(codec))
            app = Original(target, portable / "Notepad4.exe", desktop, name, encoding)
            try:
                if encoding == "ansi":
                    app.command(IDM_ENCODING_ANSI)
                    # Like the existing Base64 original capture, choose ANSI
                    # on an empty file before setting its native document bytes.
                    app.reset(content.encode("ascii"), 4 + len(expression), 4)
                app.send(SCI_SETSEL, 4 + len(expression), 4)
                before = app.state()
                app.command(CALCULATE)
                after = app.state()
                app.command(IDM_FILE_SAVE)
                saved = target.read_bytes().hex()
                app.send(SCI_UNDO)
                undo = app.state()
                app.command(IDM_FILE_SAVE)
                undo_saved = target.read_bytes().hex()
                app.send(SCI_REDO)
                redo = app.state()
                app.command(IDM_FILE_SAVE)
                rows.append(
                    dict(
                        encoding=encoding,
                        expression=expression,
                        before=before,
                        after=after,
                        saved=saved,
                        undo=undo,
                        undo_saved=undo_saved,
                        redo=redo,
                        redo_saved=target.read_bytes().hex(),
                    )
                )
                if encoding == "ansi" and app.send(SCI_GETCODEPAGE) == 936:
                    expression = '"' + "中" * 500 + '".repeat(7)'
                    data = expression.encode("gbk")
                    app.reset(data, 0, len(data))
                    app.command(IDM_FILE_SAVE)
                    before = app.state()
                    original_bytes = target.read_bytes().hex()
                    app.command(CALCULATE)
                    after = app.state()
                    app.command(IDM_FILE_SAVE)
                    rows.append(
                        dict(
                            mode="ansi-result-capacity",
                            before=before,
                            after=after,
                            before_saved=original_bytes,
                            saved=target.read_bytes().hex(),
                        )
                    )
            finally:
                app.close()
    (output / "reference.json").write_text(
        json.dumps(
            dict(
                revision="89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
                executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                cases=rows,
            ),
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
finally:
    user.CloseDesktop(desktop)
