"""Capture pinned original CSV full styles and line states through public messages."""

import argparse
import ctypes as c
import hashlib
import json
import shutil
import time
from ctypes import wintypes as w
from pathlib import Path

import original_host as host

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--exe", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--options", action="store_true")
args = parser.parse_args()
root = args.output.resolve()
portable = root / "original-portable"
portable.mkdir(parents=True, exist_ok=True)
exe = portable / "Notepad4.exe"
shutil.copy2(args.exe, exe)
(portable / "Notepad4.ini").write_text("[Settings]\nSaveSettings=0\nWordWrap=0\nWarnLineEndings=0\n", encoding="utf-16")
user = host.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
name = "wxnotepad-original-csv-280"
desktop = user.CreateDesktopW(name, None, None, 0, 0x10000000, None)
assert desktop
fixtures = [
    ("columns.csv", "一,二,3,4,5,6,7,8,9,10,11,12\r\na,,c,,,f\n", b"3"),
    ("quotes.csv", 'a,"b,c","d\r\ne","f""g",h\r\na,"unfinished\n界,z', b'"b'),
    ("tabs.tsv", 'one\ttwo\tthree\r\na\t"b\tc"\t\n空\t\t末\r', b'"b'),
    ("semicolon.csv", "a;b;c\nb;d;e\n", b";"),
    ("pipe.csv", "a|b|c\rb|d|e\r", b"|"),
    ("priority.csv", "a;b|c,d\r\ne;f|g,h\r\n", b";"),
    ("formula.csv", 'a, ="b,c", "d,e",f\nx,\\"q,r,s\n', b"="),
    ("rows.csv", "".join(f'{i},"a,b",c\n' for i in range(205)), b"100"),
]
if args.options:
    fixtures = [
        ("merge.csv", "a,,,b,c\n1,,,2,3\n", b",", (100, 110, False, True, "")),
        ("single.csv", "a,'b,c','d\ne',f\n", b"'", (100, 111, False, False, "")),
        ("escape.csv", 'a,"b' + chr(92) + '"c,d",e\n1,2,3\n', bytes([92]), (100, 110, True, False, "")),
        ("none.csv", 'a,"b,c",d\na,back' + chr(92) + ",slash,e\n", b'"', (100, 112, False, False, "")),
        ("space.csv", "a  b c\n1  2 3\n", b" ", (103, 110, False, True, "")),
        ("custom.csv", 'a:b:"c:d"\n1:2:3\n', b":", (105, 110, False, False, ":more")),
        ("invalid.csv", "a,b,c\n1,2,3\n", b",", (105, 110, False, False, "界")),
        ("pipe-option.csv", "a|b|c\n1|2|3\n", b"|", (104, 110, False, False, "")),
        ("tab-option.csv", "a\tb\tc\n1\t2\t3\n", b"\t", (102, 110, False, False, "")),
        ("semicolon-option.csv", "a;b;c\n1;2;3\n", b";", (101, 110, False, False, "")),
    ]
user.GetDlgItem.argtypes = [w.HWND, c.c_int]
user.GetDlgItem.restype = w.HWND


def options(app, selection):
    user.PostMessageW(app.hwnd, 0x111, 40702, 0)
    deadline = time.monotonic() + 5
    dialog = None
    while time.monotonic() < deadline and not dialog:

        def window(hwnd, _):
            nonlocal dialog
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            title = c.create_unicode_buffer(100)
            user.GetWindowTextW(hwnd, title, 100)
            if pid.value == app.process.pid and title.value == "CSV Options":
                dialog = hwnd
            return True

        user.EnumDesktopWindows(desktop, host.CALLBACK(window), 0)
        time.sleep(0.02)
    assert dialog, "original CSV Options appeared"
    delimiter, qualifier, escape, merge, custom = selection
    for identifier in (delimiter, qualifier):
        user.SendMessageW(user.GetDlgItem(dialog, identifier), 0xF5, 0, 0)
    for identifier, checked in ((109, escape), (108, merge)):
        user.SendMessageW(user.GetDlgItem(dialog, identifier), 0xF1, int(checked), 0)
    value = c.create_unicode_buffer(custom)
    user.SendMessageW(user.GetDlgItem(dialog, 107), 0xC, 0, c.addressof(value))
    user.SendMessageW(dialog, 0x111, 1, 0)


rows = []
try:
    for fixture in fixtures:
        filename, text, token = fixture[:3]
        raw = text.encode("utf-8")
        path = portable / filename
        path.write_bytes(raw)
        app = host.Original(path, exe, desktop, name)
        try:
            assert app.send(4002) == 223  # pinned SCLEX_CSV
            if len(fixture) == 4:
                options(app, fixture[3])
            position = raw.index(token)

            def snapshot(stage):
                app.send(4003, 0, -1)
                data = app.text()
                return dict(
                    stage=stage,
                    text=data.hex(),
                    styles=bytes(app.send(2038, i) & 255 for i in range(len(data))).hex(),
                    lines=[app.send(2093, i) for i in range(app.send(2154))],
                    folds=[app.send(2223, i) for i in range(app.send(2154))],
                )

            records = [snapshot("base")]
            app.send(2175)
            app.send(2645, position, 1)
            records.append(snapshot("edit"))
            app.send(2176)
            records.append(snapshot("undo"))
            app.send(2011)
            records.append(snapshot("redo"))
            rows.append(
                dict(
                    name=filename, position=position, states=records, options=fixture[3] if len(fixture) == 4 else None
                )
            )
        finally:
            app.close()
finally:
    user.CloseDesktop(desktop)
(root / "original.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
(root / "provenance.json").write_text(
    json.dumps(
        dict(
            revision="89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
            executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
            source="Notepad4 pinned owned reference executable",
            license="licenses/Notepad4.txt",
        ),
        indent=2,
    )
    + "\n",
    encoding="utf-8",
)
print(f"PASS: captured {len(rows)} original CSV fixtures and four complete states each")
