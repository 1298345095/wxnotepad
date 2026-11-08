"""Verify special insertion in independent application processes and saved files."""
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import time

from resource_process import command, wait_state
from settings_process import Application


def request(app, operation, value=""):
    app.sequence += 1
    command(app.folder, f"{app.sequence}\n{operation}\n{value}\n")
    app.state = wait_state(app.folder, app.process, lambda state: state["sequence"] == str(app.sequence))
    assert app.state["result"] == "ok", app.state


def insert_menu(app, keys, label):
    # The GUI matrix drives native GTK/WM_COMMAND entry points on every backend.
    # This independent process additionally exercises physical X11 menu mnemonics.
    if sys.platform == "win32" or app.state["backend"] == "GdkWaylandDisplay":
        app.named_menu("Edit", label)
        return
    focus = Path(os.environ["WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS"])
    app.state = wait_state(app.folder, app.process, lambda state: state["active"] == "1" and state["focus"] == "1")
    # Preserve native menu focus throughout the submenu mnemonic sequence.
    for gesture in ("key:" + key for key in keys):
        focus.write_text(gesture, encoding="utf-8")
        deadline = time.monotonic() + 3
        while focus.read_text(encoding="utf-8") and time.monotonic() < deadline:
            assert app.process.poll() is None, "owned app remains running during native menu input"
            time.sleep(.01)
        assert not focus.read_text(encoding="utf-8"), f"private driver acknowledged {gesture}"
    app.state = wait_state(app.folder, app.process, lambda state: state["dirty"] == "1")


def main():
    source = Path(sys.argv[1]).resolve()
    fixtures = []
    for line in Path(sys.argv[2]).read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            label, hexadecimal, _ = line.split("\t")
            fixtures.append((label, bytes.fromhex(hexadecimal)))
    with tempfile.TemporaryDirectory(prefix="wxnotepad-special-insert-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        (folder / "Notepad4.ini").write_text("[Settings]\nSaveSettings=0\nWarnLineEndings=0\n", encoding="utf-16")
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        document = folder / "插入😀.txt"
        original = b""
        document.write_bytes(original)
        app = Application(executable, folder, environment, document)
        try:
            insert_menu(app, ["alt+e", "n", "Right", "g"], "New GUID")
            request(app, "save")
            generated = document.read_bytes()
            assert re.fullmatch(rb"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", generated), generated
            app.named_menu("Edit", "Undo")
            request(app, "save")
            assert document.read_bytes() == original
            app.named_menu("Edit", "Redo")
            request(app, "save")
            assert document.read_bytes() == generated, "redo retains the same generated UUID"
            for index, (label, expected) in enumerate(fixtures):
                document = folder / f"控制-{index}😀.txt"
                document.write_bytes(original)
                request(app, "open", document)
                eol = app.state["eol_mode"]
                if index == 0:
                    insert_menu(app, ["alt+e", "n", "Right", "c", "w"], label)
                else:
                    app.named_menu("Edit", label)
                assert app.state["eol_mode"] == eol, "insertion retains EOL metadata"
                request(app, "save")
                assert document.read_bytes() == expected, (label, document.read_bytes())
                app.named_menu("Edit", "Undo")
                request(app, "save")
                assert document.read_bytes() == original
                app.named_menu("Edit", "Redo")
                request(app, "save")
                assert document.read_bytes() == expected
            app.close()
        finally:
            app.stop()
    print("PASS: independent process GUID/control commands, Unicode paths and exact saved Undo/Redo bytes")


if __name__ == "__main__":
    main()
