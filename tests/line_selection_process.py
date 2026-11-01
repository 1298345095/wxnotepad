"""Verify original LineSelection loading and saving through application processes."""
import os
from pathlib import Path
import shutil
import sys
import tempfile

from settings_process import Application


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-line-selection-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        config.write_text("[Settings]\nLineSelection=2normal\nUnrelated=keep\n", encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["line_selection"] == "2", app.state
            app.close()
        finally:
            app.stop()
        assert "LineSelection=2" in config.read_text(encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["line_selection"] == "2", app.state
            app.menu("Old Visual Studio", "Edit")
            assert app.state["line_selection"] == "3", app.state
            app.menu("Save Settings On Exit")
            app.close()
        finally:
            app.stop()
        assert "LineSelection=2" in config.read_text(encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["line_selection"] == "2", app.state
            app.menu("None", "Edit")
            assert app.state["line_selection"] == "0", app.state
            app.menu("Save Settings Now")
            app.close()
        finally:
            app.stop()
        assert "LineSelection=0" in config.read_text(encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["line_selection"] == "0", app.state
            app.menu("Visual Studio", "Edit")
            assert app.state["line_selection"] == "1", app.state
            app.menu("Save Settings Now")
            app.menu("Normal", "Edit")
            assert app.state["line_selection"] == "2", app.state
            app.close()
        finally:
            app.stop()
        saved = config.read_text(encoding="utf-16")
        assert "LineSelection=" not in saved and "Unrelated=keep" in saved, saved
        for value, expected in (("invalid", "1"), ("0", "0"), ("3suffix", "3"), ("9", "9"), ("-1", "-1")):
            config.write_text(f"[Settings]\nLineSelection={value}\n", encoding="utf-16")
            app = Application(executable, folder, environment)
            try:
                assert app.state["line_selection"] == expected, app.state
                app.close()
            finally:
                app.stop()
    print("PASS: LineSelection original modes/default, integer parsing, menu selection and settings lifecycle")


if __name__ == "__main__":
    main()
