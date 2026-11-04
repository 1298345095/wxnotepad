"""Verify original pre-save cleanup settings through actual application processes."""
import os
from pathlib import Path
import shutil
import sys
import tempfile

from settings_process import Application
from resource_process import command, wait_state


def main():
    source = Path(sys.argv[1]).resolve()
    original = b"a \t\r\nb \t\nc \t\nlast \t"
    cases = (
        ("", False, False),
        ("FixLineEndings=invalid\nFixTrailingBlanks=invalid\n", False, False),
        ("FixLineEndings=1suffix\nFixTrailingBlanks=0suffix\n", True, False),
        ("FixLineEndings=0\nFixTrailingBlanks=1\n", False, True),
        ("FixLineEndings=1\nFixTrailingBlanks=1\n", True, True),
    )
    expected = {
        (False, False): original,
        (True, False): b"a \t\nb \t\nc \t\nlast \t",
        (False, True): b"a\r\nb\nc\nlast",
        (True, True): b"a\nb\nc\nlast",
    }
    with tempfile.TemporaryDirectory(prefix="wxnotepad-cleanup-settings-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        document = folder / "input.txt"
        for fields, fix, strip in cases:
            config.write_text("[Settings]\nWarnLineEndings=0\nUnrelated=keep\n" + fields, encoding="utf-16")
            document.write_bytes(original)
            app = Application(executable, folder, environment, document)
            try:
                app.sequence += 1
                command(folder, f"{app.sequence}\nsave\n")
                app.state = wait_state(folder, app.process, lambda state: state["sequence"] == str(app.sequence))
                assert app.state["result"] == "ok" and app.state["dirty"] == "0", app.state
                assert document.read_bytes() == expected[fix, strip], (fields, document.read_bytes())
                app.close()
            finally:
                app.stop()
            saved = config.read_text(encoding="utf-16")
            assert ("FixLineEndings=1" in saved) == fix, saved
            assert ("FixTrailingBlanks=1" in saved) == strip, saved
            assert ("FixLineEndings=" in saved) == fix, saved
            assert ("FixTrailingBlanks=" in saved) == strip, saved
            assert "Unrelated=keep" in saved, saved
    print("PASS: original FixLineEndings/FixTrailingBlanks defaults, prefix parsing, startup application, persistence and default omission")


if __name__ == "__main__":
    main()
