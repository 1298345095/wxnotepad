"""Freeze original Diff load/tab and mixed-EOL default decisions privately."""

import argparse
import ctypes as c
import hashlib
import json
import shutil
import time
from ctypes import wintypes as w
from enum import IntEnum
from pathlib import Path

import original_host as host


class Sci(IntEnum):
    GetTabWidth = 2121
    GetIndent = 2123
    GetUseTabs = 2125
    GetTabIndents = 2261


WM_COMMAND = 0x111
DM_GETDEFID = 0x400
IDCANCEL = 2
GENERIC_ALL_ACCESS = 0x10000000
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--exe", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
root = args.output.resolve()
portable = root / "original-portable"
portable.mkdir(parents=True, exist_ok=True)
exe = portable / "Notepad4.exe"
shutil.copy2(args.exe, exe)
user = host.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
name = "wxnotepad-original-diff-policy-268"
desktop = user.CreateDesktopW(name, None, None, 0, GENERIC_ALL_ACCESS, None)
assert desktop
variables = "# tab-width: 6; c-basic-offset: 2; indent-tabs-mode: true; c-tab-always-indent: false\n"
text = "root\n  one\n    two\n  three\n"
records = []
try:
    for kind, filename, contents in [
        ("tabs", "detected.diff", text),
        ("tabs", "variables.diff", variables + text),
        ("tabs", "variables.mak", variables + text),
        ("tabs", "variables.ini", variables + text),
        ("warning", "mixed.diff", "one\r\ntwo\nthree\rlast"),
        ("warning", "mixed.ini", "one\r\ntwo\nthree\rlast"),
    ]:
        (portable / "Notepad4.ini").write_text(
            "[Settings]\nSaveSettings=0\nWordWrap=0\nDetectIndentation=1\n"
            "TabWidth=7\nIndentWidth=3\nTabsAsSpaces=1\n"
            f"WarnLineEndings={int(kind == 'warning')}\n",
            encoding="utf-16",
        )
        path = portable / filename
        raw = contents.encode("utf-8")
        path.write_bytes(raw)
        app = host.Original(path, exe, desktop, name)
        try:
            default = 0
            if kind == "warning":
                dialogs = []

                @host.CALLBACK
                def visit(window, _):
                    pid = w.DWORD()
                    user.GetWindowThreadProcessId(window, c.byref(pid))
                    kind_name = c.create_unicode_buffer(128)
                    user.GetClassNameW(window, kind_name, len(kind_name))
                    if pid.value == app.process.pid and kind_name.value == "#32770" and user.IsWindowVisible(window):
                        dialogs.append(window)
                    return True

                deadline = time.monotonic() + 15
                while not dialogs and time.monotonic() < deadline:
                    user.EnumDesktopWindows(desktop, visit, 0)
                    time.sleep(0.03)
                assert len(dialogs) == 1, "owned original mixed-EOL warning appears"
                default = user.SendMessageW(dialogs[0], DM_GETDEFID, 0, 0) & 0xFFFF
                user.SendMessageW(dialogs[0], WM_COMMAND, IDCANCEL, 0)
            records.append(
                dict(
                    kind=kind,
                    name=filename,
                    text=raw.hex(),
                    default=default,
                    tabs=app.send(Sci.GetTabWidth),
                    indent=app.send(Sci.GetIndent),
                    useTabs=app.send(Sci.GetUseTabs),
                    tabIndents=app.send(Sci.GetTabIndents),
                    after=app.text().hex(),
                )
            )
        finally:
            app.close()
finally:
    user.CloseDesktop(desktop)
(root / "original.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
lines = ["# Independent Notepad4 89b8b36 Diff load policy; capture_build_patch_policies.py."]
for row in records:
    lines.append(
        "\t".join(
            str(row[key])
            for key in ["kind", "name", "text", "tabs", "indent", "useTabs", "tabIndents", "default", "after"]
        )
    )
(root / "build_patch_policies_reference.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
(root / "provenance.json").write_text(
    json.dumps(
        dict(
            revision="89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
            executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
            capture_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        ),
        indent=2,
    )
    + "\n",
    encoding="utf-8",
)
print("PASS: independent original Diff tab/file-variable policy and mixed-EOL default buttons", records)
