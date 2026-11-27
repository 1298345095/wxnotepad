"""Capture the pinned original Web Tools commands on an owned private desktop.

Usage: python capture_code_format.py PINNED_NOTEPAD4_EXE EXTERNAL_EVIDENCE_DIRECTORY
No clipboard or foreground input is used.
"""

import ctypes as c
import hashlib
import json
import os
import shutil
import sys
from ctypes import wintypes as w
from pathlib import Path

from original_host import Original, user

executable, output = map(Path, sys.argv[1:3])
rectangles_only = "--rectangles-only" in sys.argv[3:]
tabs_only = "--tabs-only" in sys.argv[3:]
SET_RECTANGULAR_ANCHOR = 2590
SET_RECTANGULAR_CARET = 2588
INSERT_TABS_AS_SPACES = 40417
output.mkdir(parents=True, exist_ok=True)
host = output / "original-portable"
host.mkdir(exist_ok=True)
shutil.copy2(executable, host / "Notepad4.exe")
(host / "Notepad4.ini").write_text("[Notepad4]\n[Settings]\nSaveSettings=0\n", encoding="utf-8")
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
name = "wxnotepad-code-format-" + str(os.getpid())
desktop = user.CreateDesktopW(name, None, None, 0, 0x1FF, None)
assert desktop
rows = []


def packed(state):
    return " ".join(
        str(state[key]) if key != "hex" else state[key] or "-"
        for key in ["hex", "anchor", "caret", "modified", "undo", "redo", "selections", "main", "selectionMode"]
    )


try:
    for language, suffix, command in [
        ("Css", ".css", 40703),
        ("Scss", ".scss", 40704),
        ("Less", ".less", 40705),
        ("Hss", ".hss", 40706),
        ("JavaScript", ".js", 40707),
        ("Jsx", ".jsx", 40708),
        ("TypeScript", ".ts", 40709),
        ("Tsx", ".tsx", 40710),
        ("Json", ".json", None),
    ]:
        path = host / ("fixture" + suffix)
        path.write_bytes(b"")
        original = Original(path, host / "Notepad4.exe", desktop, name)
        try:
            if command:
                original.command(command)
            if language in ["Css", "Scss", "Less", "Hss"]:
                samples = [
                    "a { color: #abc; margin: calc(100% - 2px); }",
                    "/* hello */\na > b + c ~ d || e { content: '中😀\\\"'; color: rgba(1,2,3,.5) !important; }",
                    "@media screen and (min-width:2px) { a:not([class]):hover { --x: 1; margin: var(--x) auto; } }",
                    "a {background:url('a b');} // line\nb {width: 1px;}",
                    "a {x:1\\\n2; y: -1; z: U+1234;}",
                    "/*" + "long comment " * 100 + "*/\na{color:red}",
                    "a { color:red; }" * 200,
                ]
            elif language == "Json":
                samples = [
                    '{ "a": [1, {"中😀": true}], "b": null }',
                    '{"s":"a\\"b\\n中", /* comment */ "x": 1}//line\n',
                    '{ "a" : 1 }',
                    "[ {}, [], {'a': false}, 1e-3 ]",
                    '{"long":[' + ", ".join(str(i) for i in range(200)) + "]}",
                ]
            else:
                samples = [
                    "let x = 1; // comment\nlet y = x + +1;",
                    "function f(a,b){if(a){return {x:1,y:[2,3]};}else return .5;}",
                    "const x = `中😀 ${foo({a:1})}`; const re = /a[\\/]b+/giu;",
                    "switch(x){case 1:foo();break;default:bar();} for(let i=0;i<2;++i) a[i]++;",
                    "a++ + ++b; a-- - --b; x ??= y?.z; const f=(x)=>x**2; yield* f();",
                    "const obj = {get x(){return 1}, set x(v){}, ['a']:2};",
                    "/*" + "long comment " * 100 + "*/\nlet x=1;",
                    "const x = [" + ", ".join(str(i) for i in range(200)) + "];",
                    "<div a='x y'>{foo}</div>",
                ]
            samples += ["", " ", "a b", "a  b", "x = 1", "{}", "[]", "/* comment */", "//line\n"]
            for text in [] if rectangles_only or tabs_only else samples:
                for formatting in [0, 1]:
                    for eol in [0, 1, 2]:
                        before = text.replace("\n", ["\r\n", "\r", "\n"][eol]).encode()
                        for mode in ["whole", "forward", "reverse"] if len(before) < 500 else ["whole"]:
                            anchor, caret = (0, 0) if mode == "whole" else (0, len(before))
                            if mode == "reverse":
                                anchor, caret = caret, anchor
                            original.reset(before, anchor, caret)
                            original.send(2031, eol)
                            original.send(2036, 2)
                            original.send(2124, 0)
                            original.command(40378 + formatting)
                            after = original.state()
                            original.send(2176)
                            undo = original.state()
                            original.send(2011)
                            redo = original.state()
                            rows.append(
                                [
                                    language,
                                    formatting,
                                    eol,
                                    0,
                                    mode,
                                    before.hex() or "-",
                                    anchor,
                                    caret,
                                    packed(after),
                                    packed(undo),
                                    packed(redo),
                                ]
                            )
            before = b"let a = 1;\nlet b = 2;" if language != "Json" else b'{ "a": 1, "b": 2 }'
            use_tabs = False
            for formatting in [0, 1]:
                for mode in (
                    ["rectangle"]
                    if rectangles_only
                    else ["tabs"]
                    if tabs_only
                    else ["partial", "multiple", "empty-main", "rectangle", "readonly", "tabs"]
                ):
                    anchor, caret = (2, len(before) - 2) if mode == "partial" else (len(before), 0)
                    if mode == "empty-main":
                        anchor = caret = 3
                    original.reset(before, anchor, caret)
                    original.send(2031, 2)
                    original.send(2036, 2)
                    if use_tabs != (mode == "tabs"):
                        original.command(INSERT_TABS_AS_SPACES)
                        use_tabs = mode == "tabs"
                    assert bool(original.send(2125)) == use_tabs
                    if mode in ["multiple", "empty-main"]:
                        original.send(2563, 1)
                        original.send(2573, 7, 4)
                        original.send(2574, 0)
                    if mode == "rectangle":
                        original.send(2422, 1)
                        original.send(SET_RECTANGULAR_ANCHOR, anchor)
                        original.send(SET_RECTANGULAR_CARET, caret)
                    original.send(2171, int(mode == "readonly"))
                    original.command(40378 + formatting)
                    after = original.state()
                    original.send(2176)
                    undo = original.state()
                    original.send(2011)
                    redo = original.state()
                    rows.append(
                        [
                            language,
                            formatting,
                            2,
                            int(mode == "tabs"),
                            mode,
                            before.hex(),
                            anchor,
                            caret,
                            packed(after),
                            packed(undo),
                            packed(redo),
                        ]
                    )
        finally:
            original.close()
        print(language, len(rows), flush=True)
finally:
    user.CloseDesktop(desktop)

(output / "code_format_reference.tsv").write_text(
    "# language pretty eol tabs mode input anchor caret [after undo redo: bytes anchor caret modified undo redo selections main mode]\n"
    + "\n".join(" ".join(map(str, row)) for row in rows)
    + "\n",
    encoding="utf-8",
)
(output / "provenance.json").write_text(
    json.dumps(
        {
            "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
            "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "capture_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "cases": len(rows),
            "method": "independent original private desktop; public scheme/menu and Scintilla messages; no clipboard",
        },
        indent=2,
    )
    + "\n",
    encoding="utf-8",
)
print("PASS", len(rows), flush=True)
