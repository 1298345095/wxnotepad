"""Capture fold command output from the pinned original Windows application."""

import argparse
import csv
import ctypes as c
import hashlib
import json
import os
import shutil
from ctypes import wintypes as w
from pathlib import Path

from original_host import Original, user

REVISION = "89b8b36e0ee2c23f73be922315f17261ccfbbe1a"
COMMANDS = {
    "default": 40454,
    "all": 40455,
    "block": 40466,
    "current": 40449,
    **{f"level{level}": 40455 + level for level in range(1, 11)},
}
CPP = (
    "before();\nvoid deep() {\n"
    + "".join("  " * level + "if (flag) {\n" for level in range(1, 11))
    + "  " * 11
    + "deepest();\n"
    + "".join("  " * level + "}\n" for level in range(10, -1, -1))
    + "outside();\nvoid sibling() {\n  if (flag) {\n    leaf();\n  }\n}\nafter();\n"
)
PYTHON = (
    "before()\nclass First:\n  def run(self):\n       if True:\n            call()\n       tail()\n"
    "  reset()\noutside()\nclass Second:\n    def run(self):\n         leaf()\n    end()\nafter()\n"
    "def independent():\n   def helper():\n      work()\n   helper()\ndone()\n"
)
MARKDOWN = (
    "before\n# First\n## Empty\n## Body\ntext\n### Child\nbody\n```cpp\ncode();\n```\n# Second\nother\n## Child\ntail\n"
)
SCROLL = "before();\n" * 22 + "void distant() {\n  call();\n}\n" + "after();\n" * 40
LEGACY = "void outer() {\n  if (true) {\n    call();\n  }\n  tail();\n}\nafter();\n"
FIXTURES = {
    "cpp": (
        "cpp",
        CPP,
        [
            ("all", 0),
            ("all", None),
            ("block", 25),
            ("all", 0),
            ("all", None),
            ("block", 0),
            ("current", 0),
            ("block", CPP.splitlines().index("after();")),
            ("block", 3),
            ("block", None),
            ("current", 3),
            ("current", None),
            *[(f"level{level}", 0) for level in range(10, 0, -1)],
            ("default", 0),
            ("default", None),
            ("hide", None),
            *[(name, None) for name in COMMANDS],
            ("show", None),
        ],
    ),
    "python": (
        "py",
        PYTHON,
        [
            ("level2", 0),
            ("level2", None),
            ("block", 9),
            ("level2", 0),
            ("level2", None),
            ("current", 9),
            ("current", None),
            ("block", 4),
            ("block", None),
            ("current", 4),
            ("current", None),
            ("default", 0),
            ("default", None),
            ("all", 8),
            ("all", None),
        ],
    ),
    "markdown": (
        "md",
        MARKDOWN,
        [
            ("default", 0),
            ("default", None),
            ("block", 12),
            ("level2", 0),
            ("level2", None),
            ("current", 6),
            ("current", None),
            ("all", 12),
            ("all", None),
        ],
    ),
    "viewport": ("cpp", SCROLL, [("all", 23), ("all", None), ("block", 22), ("block", None)]),
    "legacy-hidden": ("cpp", LEGACY, [("all", 2), ("all", None)]),
    "history": ("cpp", LEGACY, [("all", 2), ("all", None), ("default", 1), ("default", None)]),
    "childless": ("cpp", "standalone();\n" + LEGACY, [("block", 0), ("all", 0), ("all", None), ("block", 0)]),
}


def resolve_menu_functions():
    user.GetMenu.argtypes = [w.HWND]
    user.GetMenu.restype = w.HMENU
    user.GetMenuItemCount.argtypes = [w.HMENU]
    user.GetMenuItemID.argtypes = [w.HMENU, c.c_int]
    user.GetSubMenu.argtypes = [w.HMENU, c.c_int]
    user.GetSubMenu.restype = w.HMENU
    user.GetMenuStringW.argtypes = [w.HMENU, w.UINT, w.LPWSTR, c.c_int, w.UINT]
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]


def read_menu(user, menu):
    items = []
    for position in range(user.GetMenuItemCount(menu)):
        label = c.create_unicode_buffer(128)
        user.GetMenuStringW(menu, position, label, len(label), 0x400)
        submenu = user.GetSubMenu(menu, position)
        items.append((user.GetMenuItemID(menu, position), label.value, read_menu(user, submenu) if submenu else []))
    return items


def editor_state(original):
    send = original.send
    lines = send(2154)
    anchor, caret = send(2009), send(2008)
    return dict(
        expanded="".join(str(send(2230, line)) for line in range(lines)),
        visible="".join(str(send(2228, line)) for line in range(lines)),
        anchor=anchor,
        caret=caret,
        reversed=int(anchor > caret),
        first=send(2152),
        xoffset=send(2398),
        modified=send(2159),
        undo=send(2174),
        redo=send(2016),
        text=original.text().hex(),
    )


def capture_fixture(executable, output, desktop, desktop_name, name, extension, text, actions):
    fixture = output / name
    fixture.mkdir()
    program = fixture / "Notepad4.exe"
    shutil.copy2(executable, program)
    (fixture / "Notepad4.ini").write_text(
        "[Settings]\nWordWrap=0\nShowCodeFolding=1\nShowToolbar=0\nShowStatusbar=0\n"
        "RenderingTechnology=0\nWarnLineEndings=0\nSaveSettings=0\n",
        encoding="utf-16",
    )
    document = fixture / f"fixture.{extension}"
    document.write_bytes(text.encode("ascii"))
    original = Original(document, program, desktop, desktop_name)
    send = original.send
    records = []
    try:
        assert original.text() == text.encode("ascii"), "original editor loaded the entire fixture"
        send(4003, 0, -1)
        send(2175)
        send(2014)
        if name == "childless":
            send(2222, 0, 0x2400)
            assert send(2224, 0, -1) == 0, "first header has no effective child"
        if name == "history":
            for addition in (b"first", b"second"):
                send(2078)
                send(2282, len(addition), original.put(addition))
                send(2079)
            send(2176)
            assert send(2159) and send(2174) and send(2016), "retain dirty text, Undo and Redo"
        if name == "viewport":
            assert user.SetWindowPos(original.editor, None, 0, 0, 700, send(2279, 0) * 14, 0x16)
        menu = read_menu(user, user.GetMenu(original.hwnd))
        geometry = {"rows": send(2370), "levels": [send(2223, line) for line in range(send(2154))], "lexer": send(4002)}
        for action, caret_line in actions:
            if caret_line is not None:
                start = send(2167, caret_line)
                if name in ("legacy-hidden", "history"):
                    start += 4
                send(2160, min(start + 2, send(2006)), start)
            if name == "viewport" and not records:
                send(2613, 5)
                send(2397, 41)
            before = editor_state(original)
            identifier = 40452 if action in ("hide", "show") else COMMANDS[action]
            original.command(identifier)
            records.append(dict(action=action, caret_line=caret_line, before=before, after=editor_state(original)))
    finally:
        original.close()
    assert document.read_bytes() == text.encode("ascii"), "fold commands never change the file"
    return dict(name=name, extension=extension, text=text, geometry=geometry, menu=menu, records=records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir()
    resolve_menu_functions()
    desktop_name = f"wxnotepad-fold-reference-{os.getpid()}"
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x1FF, None)
    if not desktop:
        raise c.WinError(c.get_last_error())
    try:
        fixtures = [
            capture_fixture(executable, output, desktop, desktop_name, name, *fixture)
            for name, fixture in FIXTURES.items()
        ]
    finally:
        user.CloseDesktop(desktop)
    report = dict(
        revision=REVISION, executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(), fixtures=fixtures
    )
    (output / "original.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_fixture(output / "fold_commands.tsv", fixtures)
    print(f"Captured {sum(len(fixture['records']) for fixture in fixtures)} Notepad4 fold transitions.")


def write_fixture(path, fixtures):
    with path.open("w", encoding="utf-8", newline="") as stream:
        stream.write(f"# Captured from Notepad4 {REVISION}.\n")
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        for fixture in fixtures:
            source = fixture["text"].encode("ascii").hex()
            writer.writerow(
                [
                    "fixture",
                    fixture["name"],
                    fixture["extension"],
                    source,
                    fixture["geometry"]["rows"],
                    fixture["geometry"]["lexer"],
                ]
            )
            for record in fixture["records"]:
                state = dict(record["after"])
                if state["text"] == source:
                    state["text"] = "-"
                writer.writerow(
                    [
                        record["action"],
                        record["caret_line"] if record["caret_line"] is not None else -1,
                        *state.values(),
                    ]
                )


if __name__ == "__main__":
    main()
