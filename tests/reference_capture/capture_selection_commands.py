"""Capture original selection commands on an owned private Windows desktop."""

import ctypes as c
import hashlib
import json
import os
import shutil
import sys
from ctypes import wintypes as w
from pathlib import Path

from original_host import Original, user

DOCUMENTS = [
    "\t  alpha beta  \r\n \t\r\n中文😀 next\nlast",
    "header {\n  hidden word\n}\n" + "  long 中文😀 word " * 35 + "\nlast",
]
COMMANDS = [40310, 40311, 40312, 40390, 40389]


def write_rectangle_header(fixtures, header):
    rows = [
        "// Captured original rectangle input ranges; see --verify-rectangles and selection-commands.md.",
        "#pragma once",
        '#include "selection_commands_reference.h"',
        "namespace SelectionReference {",
        "struct RectangleInput { int document, mode; long anchor, caret; int selections, main; std::array<Range, 2> ranges; };",
    ]
    fixtures = [row for row in fixtures if row["command"] == 40312 and row["repetition"] == 0]
    rows += [f"inline constexpr std::array<RectangleInput, {len(fixtures)}> rectangleInputs = {{{{"]
    for row in fixtures:
        before = row["before"]
        values = [row[key] for key in ["document", "mode", "anchor", "caret"]]
        values += [before["selections"], before["main"]]
        ranges = before["ranges"] + [[0, 0, 0, 0]] * (2 - len(before["ranges"]))
        literals = ", ".join("{" + ", ".join(map(str, item)) + "}" for item in ranges)
        rows.append("    {" + ", ".join(map(str, values)) + ", {{" + literals + "}}},")
    rows += ["}};", "}", ""]
    header.write_text("\n".join(rows), encoding="utf-8")


def verify_rectangles(original, output, header):
    from original_host import kernel

    send = original.send
    send(2486, 32, original.memory)  # STYLEGETFONT, STYLE_DEFAULT
    font = c.create_string_buffer(512)
    assert kernel.ReadProcessMemory(original.handle, original.memory, font, 512, None)
    metadata = dict(font=font.value.decode("utf-8"), size=send(2485, 32), tab_width=send(2121), technology=send(2631))

    def state():
        value = original.state()
        value["ranges"] = [[send(2579, i), send(2577, i), send(2583, i), send(2581, i)] for i in range(send(2570))]
        value["rectangle"] = [send(2591), send(2589), send(2595), send(2593)]
        return value

    results = []
    for document, text in enumerate(DOCUMENTS):
        for anchor, caret in [(4, 13), (13, 4)] if document == 0 else [(4, 13), (13, 4), (34, 120), (120, 34)]:
            for identifier, mode in [(40390, 0), (40389, 0), (40312, 1), (40312, 3)]:
                original.reset(text.encode(), anchor, caret)
                original.command(40076 + mode)
                send(2268, 0)
                send(2036, 4)
                send(2422, 1)
                send(2590, anchor)
                send(2588, caret)
                send(2594, 3)
                send(2592, 2)
                for repetition in range(2 if identifier == 40312 else 1):
                    before = state()
                    original.command(identifier)
                    after = state()
                    assert before["hex"] == text.encode().hex() == after["hex"]
                    for index, range_value in enumerate(before["ranges"]):
                        if identifier == 40312 and index == after["main"]:
                            continue
                        assert range_value == after["ranges"][index], (
                            document,
                            anchor,
                            caret,
                            identifier,
                            before,
                            after,
                        )
                    assert before["rectangle"] == after["rectangle"]
                    results.append(
                        dict(
                            document=document,
                            anchor=anchor,
                            caret=caret,
                            command=identifier,
                            mode=mode,
                            repetition=repetition,
                            before=before,
                            after=after,
                        )
                    )
    (output / "rectangle-invariance.json").write_text(
        json.dumps(dict(metadata=metadata, fixtures=results), ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    write_rectangle_header(results, header.with_name("selection_rectangle_reference.h"))
    print(
        f"PASS: all {len(results)} original retained-rectangle cases preserve all retained subranges and rectangle endpoints exactly; {metadata}"
    )


def write_header(results, header):
    rows = [
        "// Captured from Notepad4 89b8b36 by reference_capture/capture_selection_commands.py.",
        "#pragma once",
        "#include <array>",
        "namespace SelectionReference {",
        "struct Range { long anchor, caret, anchorVirtual, caretVirtual; };",
        f"inline constexpr std::array<const char*, {len(DOCUMENTS)}> documents = {{{{",
    ]
    rows += ["    " + json.dumps(text, ensure_ascii=False) + "," for text in DOCUMENTS]
    rows += [
        "}};",
        "struct Fixture { int document, command, mode, setup; long anchor, caret; int repetition; long expectedAnchor, expectedCaret; int selectionMode, selections, main, anchorVirtual, caretVirtual; const char* visible; std::array<Range, 2> ranges; };",
        f"inline constexpr std::array<Fixture, {len(results)}> fixtures = {{{{",
    ]
    for row in results:
        after = row["after"]
        values = [row[key] for key in ["document", "command", "mode", "setup", "anchor", "caret", "repetition"]]
        values += [
            after[key]
            for key in ["anchor", "caret", "selectionMode", "selections", "main", "anchorVirtual", "caretVirtual"]
        ]
        ranges = after["ranges"] + [[0, 0, 0, 0]] * (2 - len(after["ranges"]))
        literals = ", ".join("{" + ", ".join(map(str, item)) + "}" for item in ranges)
        rows.append(
            "    {"
            + ", ".join(map(str, values))
            + ", "
            + json.dumps("".join(map(str, after["visible"])))
            + ", {{"
            + literals
            + "}}},"
        )
    rows += ["}};", "}", ""]
    header.write_text("\n".join(rows), encoding="utf-8")


def main():
    source, output, header = map(Path, sys.argv[1:4])
    output.mkdir(parents=True, exist_ok=True)
    portable = output / "original-portable"
    portable.mkdir(exist_ok=True)
    executable = portable / "Notepad4.exe"
    shutil.copy2(source, executable)
    (portable / "Notepad4.ini").write_text(
        "[Settings]\nSaveSettings=0\nSelectOption=15\nWordWrap=0\nLineSelection=0\nTabWidth=4\n", encoding="utf-16"
    )
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    name = "selection-command-reference-" + str(os.getpid())
    desktop = user.CreateDesktopW(name, None, None, 0, 0x1FF, None)
    if not desktop:
        raise c.WinError(c.get_last_error())
    original = Original(None, executable, desktop, name)
    user.WaitForInputIdle.argtypes = [w.HANDLE, w.DWORD]
    results = []
    try:
        assert user.WaitForInputIdle(original.process.handle, 10000) == 0, "original startup reached input idle"
        if "--verify-rectangles" in sys.argv[4:]:
            verify_rectangles(original, output, header)
            return
        for document, text in enumerate(DOCUMENTS):
            data = text.encode()
            positions = (
                [0, 1, 3, 4, 8, 9, 13, 15, 16, 18, 20, 23, 26, 30, len(data)]
                if document == 0
                else [0, 11, 24, 34, 120, 500, len(data)]
            )
            # Include only UTF-8 character boundaries.
            positions = [p for p in positions if p == len(data) or data[p] & 0xC0 != 0x80]
            selections = [(p, p) for p in positions] + [
                (4, 13),
                (13, 4),
                (3, 16),
                (16, 3),
                (0, len(data)),
                (len(data), 0),
            ]
            if document == 1:
                selections += [(34, 120), (120, 34)]
            for anchor, caret in selections:
                for command, identifier in enumerate(COMMANDS):
                    for mode in range(4) if command == 2 else [0]:
                        setups = (
                            range(6)
                            if (anchor, caret) in [(4, 13), (13, 4), (34, 120), (120, 34)]
                            else ([0, 5] if document == 1 and caret in [120, 500] else [0])
                        )
                        for setup in setups:
                            original.reset(data, anchor, caret)
                            send = original.send
                            original.command(40076 + mode)
                            send(2268, setup == 5)  # word wrap
                            if setup == 1:
                                send(2422, 1)
                                send(2590, anchor)
                                send(2588, caret)
                                send(2594, 3)
                                send(2592, 2)
                            elif setup == 2:
                                send(2573, 0, 0)
                            elif setup == 3:
                                send(2171, 1)
                            elif setup == 4:
                                send(2222, 0, 0x2400)
                                send(2222, 1, 0x401)
                                send(2222, 2, 0x400)
                                send(2232, 0, 0)
                                send(2227, 1, 1)
                            before = original.state()
                            assert before["hex"] == data.hex()
                            assert (before["anchor"], before["caret"]) == ((0, 0) if setup == 2 else (anchor, caret))
                            for repetition in range(2 if command == 2 else 1):
                                original.command(identifier)
                                after = original.state()
                                after.update(
                                    anchorVirtual=send(2595),
                                    caretVirtual=send(2593),
                                    firstVisible=send(2152),
                                    xOffset=send(2398),
                                    visible=[send(2228, line) for line in range(send(2154))],
                                    ranges=[
                                        [send(2579, i), send(2577, i), send(2583, i), send(2581, i)]
                                        for i in range(send(2570))
                                    ],
                                )
                                assert after["selections"] <= 2
                                assert after["hex"] == data.hex()
                                results.append(
                                    dict(
                                        document=document,
                                        command=command,
                                        mode=mode,
                                        setup=setup,
                                        anchor=anchor,
                                        caret=caret,
                                        repetition=repetition,
                                        before=before,
                                        after=after,
                                    )
                                )
    finally:
        original.close()
        user.CloseDesktop(desktop)
    record = dict(
        revision="89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
        executable_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        documents=DOCUMENTS,
        fixtures=results,
    )
    (output / "original.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    write_header(results, header)
    print(f"Captured {len(results)} original selection cases")


if __name__ == "__main__":
    main()
