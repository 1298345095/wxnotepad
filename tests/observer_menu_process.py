"""Dispatch existing shown application menus by numeric command identity."""
import os
from pathlib import Path
import shutil
import sys
import tempfile

from resource_process import command, wait_state
from settings_process import Application

# wxWidgets' stable stock File/New command identity.
NEW_COMMAND = 5002


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-observer-menu-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        (folder / "Notepad4.ini").write_text(
            "[Settings]\nSaveSettings=0\n[Settings2]\nUILanguage=1033\n", encoding="utf-16")
        document = folder / "numeric-menu.txt"
        document.write_bytes(b"same real Save command\n")
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        app = Application(executable, folder, environment, document)
        try:
            command(folder, f"1\nmenu\n{NEW_COMMAND}\n")
            state = wait_state(folder, app.process, lambda s: s["sequence"] == "1")
            assert state["result"] == "ok", state
            assert state["length"] == "0", state
            assert document.read_bytes() == b"same real Save command\n"
            command(folder, "2\nmenu\n2147483647\n")
            state = wait_state(folder, app.process, lambda s: s["sequence"] == "2")
            assert state["result"] == "failed" and state["length"] == "0", state
            app.sequence = 2
            # Existing English callers continue through the same menu seam.
            app.named_menu("File", "New")
            app.close()
        finally:
            app.stop()
    print("PASS: real numeric New, unknown ID rejection and English label compatibility")


if __name__ == "__main__":
    main()
