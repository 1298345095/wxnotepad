"""Verify file-variable gates and DetectIndentation through real application processes."""
import os
from pathlib import Path
import shutil
import sys
import tempfile

from settings_process import Application, tabs


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-indent-settings-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        startup = folder / "sample.txt"
        startup.write_bytes(b"# tab-width: 7; indent-tabs-mode: true\nroot\n  alpha\n    beta\n")
        cases = [
            ("DetectIndentation=0\n", "", 7, 7, 1),
            ("DetectIndentation=1\n", "", 7, 7, 1),
            ("DetectIndentation=0\n", "NoFileVariables=1\n", 5, 3, 1),
            ("DetectIndentation=1\n", "NoFileVariables=1\n", 2, 2, 0),
        ]
        for settings, settings2, tab, indent, use_tabs in cases:
            config.write_text("[Settings]\nTabWidth=5\nIndentWidth=3\nUnrelated=keep\n" + settings +
                              "[Settings2]\n" + settings2, encoding="utf-16")
            app = Application(executable, folder, environment, startup)
            try:
                tabs(app.state, tab, indent, use_tabs)
                app.menu("Save Settings Now")
                app.close()
            finally:
                app.stop()
            saved = config.read_text(encoding="utf-16")
            assert "Unrelated=keep" in saved, saved
            assert ("DetectIndentation=0" in saved) == (settings == "DetectIndentation=0\n"), saved
            assert "DetectIndentation=1" not in saved, saved
            assert ("NoFileVariables=1" in saved) == bool(settings2), saved

        # An encoding tag in the beginning window prevents using tab variables at the end.
        startup.write_bytes(b"# coding: utf-8\n" + b"x" * 600 + b"\n# tab-width: 6; indent-tabs-mode: nil\n")
        for no_tags, expected in [(0, (5, 3, 1)), (1, (6, 6, 0))]:
            config.write_text("[Settings]\nTabWidth=5\nIndentWidth=3\nDetectIndentation=0\n" +
                              f"NoEncodingTags={no_tags}\n", encoding="utf-16")
            app = Application(executable, folder, environment, startup)
            try:
                tabs(app.state, *expected)
                app.close()
            finally:
                app.stop()
        # UTF-16 variables are parsed after the original Unicode-to-UTF-8 conversion.
        startup.write_text("# tab-width: 8; c-basic-offset: 3; indent-tabs-mode: nil\n", encoding="utf-16")
        app = Application(executable, folder, environment, startup)
        try:
            tabs(app.state, 8, 3, 0)
            app.close()
        finally:
            app.stop()
    print("PASS: DetectIndentation load/save/default omission, NoFileVariables, NoEncodingTags and UTF-16 variables")


if __name__ == "__main__":
    main()
