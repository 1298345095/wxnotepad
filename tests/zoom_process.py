"""Verify the original ZoomLevel preference across actual application processes."""
import os
from pathlib import Path
import shutil
import sys
import tempfile

from settings_process import Application


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-zoom-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        config.write_text("[Settings]\nZoomLevel=175%\nUnrelated=keep\n", encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["zoom"] == "175", app.state
            app.menu("Zoom In")
            assert app.state["zoom"] == "185", app.state
            app.close()
        finally:
            app.stop()
        assert "ZoomLevel=185" in config.read_text(encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["zoom"] == "185", app.state
            app.menu("Save Settings On Exit")
            app.menu("Zoom Out")
            app.close()
        finally:
            app.stop()
        assert "ZoomLevel=185" in config.read_text(encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["zoom"] == "185", app.state
            app.menu("Reset Zoom")
            app.menu("Save Settings Now")
            app.menu("Zoom In")
            app.close()
        finally:
            app.stop()
        saved = config.read_text(encoding="utf-16")
        assert "ZoomLevel=" not in saved and "Unrelated=keep" in saved, saved
        for value, expected in (("invalid", "100"), ("0", "10"), ("999", "500")):
            config.write_text(f"[Settings]\nZoomLevel={value}\n", encoding="utf-16")
            app = Application(executable, folder, environment)
            try:
                assert app.state["zoom"] == expected, app.state
                app.close()
            finally:
                app.stop()
    print("PASS: ZoomLevel prefix parsing, restoration, limits, disabled exit save and explicit default reset")


if __name__ == "__main__":
    main()
