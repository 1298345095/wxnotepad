"""Freeze untouched pinned script-language controls on an owned private desktop."""

import argparse
import ctypes as c
import hashlib
import json
import shutil
import threading
import time
from ctypes import wintypes as w
from pathlib import Path

import original_host as host

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--exe", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
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
        "complete.rb",
        "# comment 界\nmodule Sample\n  class Item\n    def run(name)\n"
        "      @value = %Q{hello #{name}}\n      words = %w(one two)\n"
        "      expr = %r{a+b}i\n      symbol = %s(value)\n      raw = %q{raw}\n"
        "      text = <<~TEXT\nhello #{name} 界\nTEXT\n    end\n  end\nend\n"
        "=begin\ndoc 界\n=end\n__END__\ndata\n",
        "%Q",
    ),
    ("incomplete.rb", "module Sample\n  def run\n    text = <<TEXT\nunfinished 界 #{name\n", "<<"),
    (
        "complete.pl",
        '# comment 界\npackage Sample;\nsub run {\n  my $value = "hello $name";\n'
        "  my @items = qw(one two);\n  my %map = (one => 1);\n"
        "  $value =~ s/a+/b/g;\n  if ($value =~ m{foo}i) { print $value; }\n"
        '  my $text = <<"TEXT";\nhello $value 界\nTEXT\n}\n'
        "=pod\ndoc 界\n=cut\n__DATA__\ndata\n",
        "s/a+",
    ),
    ("incomplete.pm", "sub run {\n  my $text = <<TEXT;\nunfinished $name 界\n", "<<"),
    (
        "complete.tcl",
        "# comment 界\nnamespace eval Sample {\n  proc run {name} {\n"
        '    set text "hello $name [string length $name] 界"\n'
        "    set raw {braces {nested}}\n    set value ${name}\n"
        "    puts $text " + chr(92) + "\n      continued\n  }\n}\n",
        "[string",
    ),
    ("incomplete.tm", 'proc run {name} {\n  set text "unfinished $name [string length\n界\n', '"unfinished'),
]
completions = [
    ("Ruby Script", "rb", "requ", 0),
    ("Ruby Script", "rb", "$LOAD", 0),
    ("Ruby Script", "rb", "req", ord("u")),
    ("Ruby Script", "rb", "defined", 0),
    ("Perl Script", "pl", "prin", 0),
    ("Perl Script", "pl", "$FORM", 0),
    ("Perl Script", "pl", "@ARG", 0),
    ("Perl Script", "pl", "pri", ord("n")),
    ("Tcl Script", "tcl", "strin", 0),
    ("Tcl Script", "tcl", "tk_getO", 0),
    ("Tcl Script", "tcl", "stri", ord("n")),
]
characters = [
    ("Ruby Script", "rb", "value = ", ord("(")),
    ("Ruby Script", "rb", "value = ", ord('"')),
    ("Ruby Script", "rb", "value = 'raw\\", ord("'")),
    ("Ruby Script", "rb", "if ready", 13),
    ("Ruby Script", "rb", "while ready", 13),
    ("Ruby Script", "rb", "class Sample", 13),
    ("Ruby Script", "rb", "def run", 13),
    ("Perl Script", "pl", "sub run ", ord("{")),
    ("Perl Script", "pl", "$value = ", ord('"')),
    ("Perl Script", "pl", "$value = 'raw\\", ord("'")),
    ("Perl Script", "pl", "sub run {", 13),
    ("Tcl Script", "tcl", "proc run {} ", ord("{")),
    ("Tcl Script", "tcl", "set value ", ord('"')),
    ("Tcl Script", "tcl", "proc run {} {", 13),
    ("Tcl Script", "tcl", "  puts value " + chr(92), 13),
]

user = host.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
user.GetMenu.argtypes = [w.HWND]
user.GetMenu.restype = w.HMENU
user.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]
desktop_name = "wxnotepad-original-dynamic-281"
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


def command(app, identifier, rectangle):
    if not rectangle:
        app.command(identifier)
        return 0
    worker = threading.Thread(target=app.command, args=(identifier,))
    worker.start()
    dialogs = []
    deadline = time.monotonic() + 5
    while worker.is_alive() and time.monotonic() < deadline:

        def window(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(100)
            user.GetClassNameW(hwnd, name, 100)
            button = user.GetDlgItem(hwnd, 1) or user.GetDlgItem(hwnd, 2)
            if (
                pid.value == app.process.pid
                and name.value == "#32770"
                and hwnd not in dialogs
                and user.IsWindowVisible(hwnd)
                and button
            ):
                dialogs.append(hwnd)
                user.PostMessageW(hwnd, 0x111, user.GetDlgCtrlID(button), 0)
            return True

        user.EnumDesktopWindows(desktop, host.CALLBACK(window), 0)
        time.sleep(0.01)
    worker.join(timeout=1)
    assert not worker.is_alive(), "original rectangle command completed"
    return len(dialogs)


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
                        ",".join(str(app.send(2093, line)) for line in range(app.send(2154))),
                    ]
                )
        finally:
            app.close()
    cases = [("completion", *case, None, False, 0) for case in completions]
    cases += [("character", *case, None, False, 0) for case in characters]
    for scheme, extension in [("Ruby Script", "rb"), ("Perl Script", "pl"), ("Tcl Script", "tcl")]:
        for kind in ["Toggle Line Comment", "Toggle Block Comment"]:
            for readonly, mode in [(False, 0), (True, 0), (False, 1)]:
                cases.append((kind, scheme, extension, "call()\nnext()\n", 0, 0, readonly, mode))
    for kind, scheme, extension, before, character, anchor, readonly, mode in cases:
        caret = len(before.partition("|")[0].encode("utf-8"))
        before = before.replace("|", "", 1)
        if anchor is not None:
            caret = 6
        else:
            anchor = caret
        path = portable / ("operation." + extension)
        path.write_bytes(b"\xef\xbb\xbf")
        app = host.Original(path, executable, desktop, desktop_name)
        try:
            raw = before.encode("utf-8")
            app.reset(raw, anchor, caret)
            app.send(2422, mode)
            if mode:
                app.send(2590, anchor)
                app.send(2588, caret)
            app.send(2171, int(readonly))
            app.send(4003, 0, -1)
            notices = 0
            enabled = True
            if character == 13:
                app.send(2329)  # SCI_NEWLINE is the actual Enter editing command.
            elif character:
                user.SendMessageW(app.editor, 0x102, character, 0)
            else:
                identifier = {"completion": 40373, "Toggle Line Comment": 40354, "Toggle Block Comment": 40355}[kind]
                menu = user.GetMenu(app.hwnd)
                user.SendMessageW(app.hwnd, 0x116, menu, 0)  # WM_INITMENU updates the original command state.
                enabled = not (user.GetMenuState(menu, identifier, 0) & 3)
                if enabled:
                    notices = command(app, identifier, bool(mode))
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
                    str(anchor),
                    str(int(readonly)),
                    str(mode),
                    str(notices),
                    str(int(enabled)),
                    str(undo["anchor"]),
                    str(undo["caret"]),
                    str(redo["anchor"]),
                    str(redo["caret"]),
                ]
            )
            print(scheme, kind, repr(before), character, [item.decode() for item in items], flush=True)
        finally:
            app.close()
finally:
    user.CloseDesktop(desktop)

for name, rows in [("dynamic_languages_reference.tsv", states), ("dynamic_operations_reference.tsv", operations)]:
    (output / name).write_text(
        "# Frozen untouched Notepad4 89b8b36; capture_dynamic_languages.py.\n"
        + "\n".join("\t".join(row) for row in rows)
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
print("PASS: captured", len(states), "original style/fold states and", len(operations), "operations")
