"""Capture original formatter file bytes, encoding identity and equal-length no-ops."""

import ctypes as c
import hashlib
import json
import os
import shutil
import sys
from ctypes import wintypes as w
from pathlib import Path

from original_host import Original, user

# Pinned Notepad4/Scintilla and native platform protocol values.
DESKTOP_ALL_ACCESS = 0x1FF
IDM_EDIT_CODE_COMPRESS = 40378
IDM_EDIT_CODE_PRETTY = 40379
IDM_FILE_SAVE = 40004
SCI_EMPTYUNDOBUFFER = 2175
SCI_GETLENGTH = 2006
SCI_SETEOLMODE = 2031
SCI_SETSAVEPOINT = 2014
SCI_SETSEL = 2160
SCI_SETTABWIDTH = 2036
SCI_SETUSETABS = 2124

executable, output = map(Path, sys.argv[1:3])
output.mkdir(parents=True, exist_ok=True)
host = output / "original-portable"
host.mkdir(exist_ok=True)
shutil.copy2(executable, host / "Notepad4.exe")
(host / "Notepad4.ini").write_text("[Notepad4]\n[Settings]\nSaveSettings=0\n", encoding="utf-8")
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
name = "wxnotepad-code-format-files-" + str(os.getpid())
desktop = user.CreateDesktopW(name, None, None, 0, DESKTOP_ALL_ACCESS, None)
assert desktop
rows = []
try:
    for encoding, codec, marker, text in [
        ("utf-8", "utf-8", b"", '{ "中é": [1, 2], "s": "文😀" }'),
        ("utf-8-bom", "utf-8", b"\xef\xbb\xbf", '{ "中é": [1, 2] }'),
        ("utf-16le", "utf-16le", b"\xff\xfe", '{ "中é": [1, 2] }'),
        ("utf-16be", "utf-16be", b"\xfe\xff", '{ "中é": [1, 2] }'),
        ("gbk", "gbk", b"", '{ "中文é": [1, 2] }'),
        ("big5", "big5", b"", '{ "中文": [1, 2] }'),
        ("windows-1252", "cp1252", b"", '{ "é": [1, 2] }'),
    ]:
        for formatting in [0, 1]:
            for eol in [0, 1, 2]:
                path = host / "fixture.json"
                before = marker + text.encode(codec)
                path.write_bytes(before)
                original = Original(
                    path, host / "Notepad4.exe", desktop, name, "utf-8" if encoding == "utf-8-bom" else encoding
                )
                try:
                    original.send(SCI_SETEOLMODE, eol)
                    original.send(SCI_SETTABWIDTH, 2)
                    original.send(SCI_SETUSETABS, 0)
                    original.send(SCI_SETSEL, 0, original.send(SCI_GETLENGTH))
                    original.send(SCI_EMPTYUNDOBUFFER)
                    original.send(SCI_SETSAVEPOINT)
                    original.command(IDM_EDIT_CODE_COMPRESS + formatting)
                    after = original.state()
                    original.command(IDM_FILE_SAVE)
                    saved = path.read_bytes()
                    rows.append(
                        dict(
                            encoding=encoding,
                            pretty=formatting,
                            eol=eol,
                            input=before.hex(),
                            after=after,
                            saved=saved.hex(),
                        )
                    )
                finally:
                    original.close()
    path = host / "same-length.json"
    for text in [b"{}  ", b"[]  ", b"{ } ", b"[ ] "]:
        path.write_bytes(text)
        original = Original(path, host / "Notepad4.exe", desktop, name)
        try:
            original.reset(text, len(text), 0)
            original.send(SCI_SETEOLMODE, 0)
            original.command(IDM_EDIT_CODE_PRETTY)
            rows.append(
                dict(encoding="same-length", pretty=1, eol=0, input=text.hex(), after=original.state(), saved="-")
            )
        finally:
            original.close()
finally:
    user.CloseDesktop(desktop)
(output / "code_format_files_reference.tsv").write_text(
    "# encoding pretty eol disk-input buffer-after disk-saved anchor caret modified undo redo\n"
    + "\n".join(
        " ".join(
            map(
                str,
                [
                    row["encoding"],
                    row["pretty"],
                    row["eol"],
                    row["input"],
                    row["after"]["hex"] or "-",
                    row["saved"],
                    row["after"]["anchor"],
                    row["after"]["caret"],
                    row["after"]["modified"],
                    row["after"]["undo"],
                    row["after"]["redo"],
                ],
            )
        )
        for row in rows
    )
    + "\n",
    encoding="utf-8",
)
(output / "provenance.json").write_text(
    json.dumps(
        dict(
            revision="89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
            cases=len(rows),
            executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
            capture_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            observations=rows,
        ),
        indent=2,
    )
    + "\n",
    encoding="utf-8",
)
print("PASS", len(rows), flush=True)
