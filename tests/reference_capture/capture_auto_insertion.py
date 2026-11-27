"""Capture pinned Notepad4 header commands on an isolated Windows desktop."""

import json
import sys
from pathlib import Path

from character_capture import Session, prepare_host

exe, output = map(Path, sys.argv[1:3])


host = prepare_host(exe, output)

rows = []
for language, suffix in [
    ("PlainText", "txt"),
    ("Cpp", "cpp"),
    ("Python", "py"),
    ("Json", "json"),
    ("JavaScript", "js"),
    ("TypeScript", "ts"),
    ("Bash", "sh"),
    ("Html", "html"),
    ("Xml", "xml"),
    ("Rust", "rs"),
    ("Go", "go"),
    ("Lua", "lua"),
]:
    path = host / f"input.{suffix}"
    path.write_text("", encoding="utf-8")
    session = Session(host / "Notepad4.exe", path)
    cases = [("", 0, ch, False) for ch in "({[<\"'`,"]
    cases += [
        (text, pos, ch, False)
        for text, pos, ch in [
            (")", 0, "("),
            ("())", 1, "("),
            ("x", 0, "("),
            ("// comment", 10, "("),
            ('"abc"', 3, "("),
            ('"\\"', 2, '"'),
            ('"\\\\"', 3, '"'),
            ("'a'", 2, "("),
            ("someone", 7, "'"),
            ("u8", 2, "'"),
            ("L", 1, "'"),
            ("fr", 2, "'"),
            ("vector", 6, "<"),
            ("int", 3, "<"),
            ("x ", 2, "<"),
            ("a b", 1, ","),
            ("a\tb", 1, ","),
            ('""', 1, ","),
            ("''", 1, ","),
            ("a", 1, ","),
            ('"raw', 4, "`"),
            ("# comment", 9, "'"),
            ('r"\\"', 3, '"'),
        ]
    ]
    cases += [("keep", 2, "(", True)]
    cases = [(before, pos, ch, readonly, pos, -1, -1, False) for before, pos, ch, readonly in cases]
    cases += [
        ("word", 4, "(", False, 0, -1, -1, False),
        ("word", 0, "[", False, 4, -1, -1, False),
        ("a\nb", 1, "(", False, 1, 3, 3, False),
        ("a\nb", 0, "[", False, 1, 2, 3, False),
        ("abc", 1, "(", False, 1, -1, -1, True),
    ]
    for before, pos, ch, readonly, anchor, extraAnchor, extraCaret, overtype in cases:
        captured = session.capture_case(before, pos, ch, readonly, anchor, extraAnchor, extraCaret, overtype)
        saved = path.read_bytes().decode("utf-8")
        rows.append(
            dict(
                anchor=anchor,
                extraAnchor=extraAnchor,
                extraCaret=extraCaret,
                overtype=overtype,
                saved=saved,
                language=language,
                before=before,
                position=pos,
                character=ord(ch),
                readonly=readonly,
                **captured,
            )
        )
    session.close()
(output / "original.json").write_text(
    json.dumps(dict(revision="89b8b36e0ee2c23f73be922315f17261ccfbbe1a", cases=rows), indent=2) + "\n"
)
lines = [
    "// Captured from original Notepad4 89b8b36; see capture_auto_insertion.py.",
    "#pragma once",
    "#include <array>",
    "namespace AutoInsertionReference {",
    "struct Selection { intptr_t anchor, caret; };",
    "struct State { const char* text; intptr_t anchor, caret; bool modified, canUndo, canRedo; std::initializer_list<Selection> selections; };",
    "struct Case { SyntaxLanguage language; const char* before; intptr_t position; int character; bool readOnly; intptr_t anchor, extraAnchor, extraCaret; bool overtype; const char* saved; State after; std::initializer_list<State> undo, redo; };",
    f"inline const std::array<Case, {len(rows)}> cases{{{{",
]


def lit(value):
    if all(" " <= ch <= "~" for ch in value) and (
        value.count('"') > 2 or value.count("\\") > 1 or ('"' in value and "\\" in value)
    ):
        delimiter = ""
        while ")" + delimiter + '"' in value:
            delimiter += "_"
        return 'R"' + delimiter + "(" + value + ")" + delimiter + '"'
    return json.dumps(value, ensure_ascii=True)


def bool_lit(x):
    return "true" if x else "false"


def state_lit(s):
    return (
        "{"
        + lit(s[0])
        + ", "
        + ", ".join(str(x) for x in s[1:3])
        + ", "
        + ", ".join(bool_lit(x) for x in s[3:6])
        + ", {"
        + ", ".join("{" + str(a) + ", " + str(b) + "}" for a, b in s[6])
        + "}}"
    )


for r in rows:
    lines.append(
        "{SyntaxLanguage::"
        + r["language"]
        + ", "
        + lit(r["before"])
        + ", "
        + str(r["position"])
        + ", "
        + str(r["character"])
        + ", "
        + bool_lit(r["readonly"])
        + ", "
        + ", ".join(str(r[k]) for k in ["anchor", "extraAnchor", "extraCaret"])
        + ", "
        + bool_lit(r["overtype"])
        + ", "
        + lit(r["saved"])
        + ", "
        + state_lit(r["after"])
        + ", {"
        + ", ".join(map(state_lit, r["undo"]))
        + "}, {"
        + ", ".join(map(state_lit, r["redo"]))
        + "}},"
    )
lines += ["}};", "} // namespace AutoInsertionReference"]
Path(__file__).resolve().parents[1].joinpath("auto_insertion_reference.h").write_text("\n".join(lines) + "\n")
print(f"Captured {len(rows)} original cases")
