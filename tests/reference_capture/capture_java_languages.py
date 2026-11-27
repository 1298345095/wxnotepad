"""Freeze pinned original Java/C#/Kotlin controls on an owned private desktop.

Uses the untouched original executable, public editor messages and commands.
No clipboard or host input is used. The resulting TSVs are consumed verbatim
by the native application workflow fixture.
"""

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import shutil
import original_host as host

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--exe", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument(
    "--documentation-only", action="store_true", help="Capture the three interior documentation completions"
)
args = parser.parse_args()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
portable = output / "original-portable"
portable.mkdir()
executable = portable / "Notepad4.exe"
shutil.copy2(args.exe, executable)
(portable / "Notepad4.ini").write_text(
    "[Settings]\nSaveSettings=0\nWordWrap=0\nDetectIndentation=0\n"
    "AutoCompleteOption=7\nAutoCIgnoreCase=0\nLaTeXInputMethod=0\n",
    encoding="utf-16",
)

fixtures = [
    (
        "complete.java",
        "/** @param value <b>Java</b> */\n"
        "@Deprecated\nclass Sample<T extends Number> {\n"
        '  String block = """\nhello \\s界\n""";\n'
        '  String template = STR."value \\{value}";\n'
        "  <U> U convert(U value) { return value; }\n}\n",
        '"""',
    ),
    ("incomplete.java", 'class Broken<T {\n  String text = """\nunterminated\n/* incomplete comment @param\n', '"""'),
    (
        "complete.cs",
        "#region sample\n/// <summary>Sample</summary>\n"
        "[Obsolete]\nclass Sample<T> {\n"
        '  string verbatim = @"C:\\folder\\界";\n'
        '  string value = $"{1 + 2:D2}";\n'
        '  string raw = $$"""\n{{value}} "quoted"\n""";\n'
        "  void Run() { /* TODO */ }\n}\n#endregion\n",
        '"""',
    ),
    ("incomplete.csx", '#if DEBUG\n/// <summary incomplete\nvar raw = $$"""\n{{value +\n', '"""'),
    (
        "complete.kt",
        '/** @param value Kotlin */\n@Deprecated("old")\n'
        "class Sample<T> {\n  /* outer /* nested */ still comment */\n"
        '  val text = "Hello ${value + 1}: $value"\n'
        '  val raw = """\nraw \\n $value 界\n"""\n'
        "  fun `quoted name`(value: T) { println(value) }\n}\n",
        "/* nested",
    ),
    ("incomplete.kts", '#!/usr/bin/env kotlin\nval value = "${call(\n/* outer /* inner */ unfinished\n', "/* inner"),
]

# Mode is the character to type, or zero for the public Complete Word command.
completions = [
    ("Java", "java", "pri", 0),
    ("Java", "java", "@Dep", 0),
    ("Java", "java", "/** @par", 0),
    ("Java", "java", "pr", ord("i")),
    ("C#", "cs", "pub", 0),
    ("C#", "cs", "#reg", 0),
    ("C#", "cs", "/// <sum", 0),
    ("C#", "cs", "pu", ord("b")),
    ("Kotlin", "kt", "pri", 0),
    ("Kotlin", "kt", "@Dep", 0),
    ("Kotlin", "kt", "/** @par", 0),
    ("Kotlin", "kt", "pr", ord("i")),
]
documentation_completions = [
    ("Java", "java", "/** @par| */", 0),
    ("C#", "cs", "/// <sum| ", 0),
    ("Kotlin", "kt", "/** @par| */", 0),
]
completions += documentation_completions
characters = [
    ("Java", "java", "class Sample<T", ord("<")),
    ("Java", "java", '"text\\', ord('"')),
    ("C#", "cs", 'var x = @"raw\\', ord('"')),
    ("C#", "cs", "List<string", ord("<")),
    ("C#", "cs", 'var x = """raw\\', ord('"')),
    ("Kotlin", "kt", "List<String", ord("<")),
    ("Kotlin", "kt", 'val x = """raw\\', ord('"')),
    ("Kotlin", "kt", 'val x = "text\\', ord('"')),
]
if args.documentation_only:
    fixtures = []
    completions = documentation_completions
    characters = []
user = host.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
desktop_name = "wxnotepad-original-java-255"
desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
assert desktop


def candidates(app):
    words = []
    previous = -1
    while app.send(2102):
        index = app.send(2445)
        if index == previous:
            break
        previous = index
        size = app.send(2610, 0, app.memory)
        buffer = c.create_string_buffer(size + 1)
        assert host.kernel.ReadProcessMemory(app.handle, app.memory, buffer, size + 1, None)
        words.append(buffer.value)
        user.SendMessageW(app.editor, 0x100, 0x28, 0)
    return words


states = []
operations = []
try:
    for filename, text, token in fixtures:
        raw = text.encode("utf-8")
        path = portable / filename
        path.write_bytes(raw)
        app = host.Original(path, executable, desktop, desktop_name)
        try:
            position = raw.index(token.encode())
            for stage in ["base", "edit", "undo", "redo"]:
                if stage == "edit":
                    app.send(2175)
                    app.send(2645, position, 1)
                elif stage == "undo":
                    app.send(2176)
                elif stage == "redo":
                    app.send(2011)
                app.send(4003, 0, -1)
                data = app.text()
                states.append(
                    [
                        filename,
                        stage,
                        str(position),
                        data.hex(),
                        bytes(app.send(2038, index) & 255 for index in range(len(data))).hex(),
                        ",".join(str(app.send(2223, line)) for line in range(app.send(2154))),
                    ]
                )
        finally:
            app.close()
    for kind, cases in [("completion", completions), ("character", characters)]:
        for scheme, extension, before, character in cases:
            caret = len(before.partition("|")[0].encode("utf-8"))
            before = before.replace("|", "", 1)
            path = portable / ("operation." + extension)
            path.write_bytes(b"\xef\xbb\xbf")
            app = host.Original(path, executable, desktop, desktop_name)
            try:
                raw = before.encode("utf-8")
                app.reset(raw, caret, caret)
                app.send(4003, 0, -1)
                if character:
                    user.SendMessageW(app.editor, 0x102, character, 0)
                else:
                    app.command(40373)
                changed = app.state()
                items = candidates(app)
                if items and kind == "completion":
                    app.send(2108, 0, app.put(items[0] + b"\0"))
                    user.SendMessageW(app.editor, 0x100, 0x0D, 0)
                confirmed = app.state()
                app.send(2101)
                app.send(2176)
                undo = app.state()
                app.send(2011)
                redo = app.state()
                operations.append(
                    [
                        scheme,
                        kind,
                        raw.hex(),
                        str(character),
                        changed["hex"],
                        str(changed["anchor"]),
                        str(changed["caret"]),
                        ",".join(item.hex() for item in items),
                        confirmed["hex"],
                        str(confirmed["anchor"]),
                        str(confirmed["caret"]),
                        undo["hex"],
                        redo["hex"],
                        str(caret),
                    ]
                )
                print(scheme, kind, repr(before), character, [item.decode() for item in items], flush=True)
            finally:
                app.close()
finally:
    user.CloseDesktop(desktop)

(output / "java_languages_reference.tsv").write_text(
    "# Frozen untouched Notepad4 89b8b36; capture_java_languages.py.\n"
    + "\n".join("\t".join(row) for row in states)
    + "\n",
    encoding="utf-8",
    newline="\n",
)
(output / "java_operations_reference.tsv").write_text(
    "# Frozen original native completion and typed-character results.\n"
    + "\n".join("\t".join(row) for row in operations)
    + "\n",
    encoding="utf-8",
    newline="\n",
)
(output / "provenance.json").write_text(
    json.dumps(
        {
            "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
            "executable": str(args.exe.resolve()),
            "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "capture_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "style_states": len(states),
            "operations": len(operations),
        },
        indent=2,
    )
    + "\n",
    encoding="utf-8",
)
print("PASS: captured", len(states), "independent original states and", len(operations), "native operations")
