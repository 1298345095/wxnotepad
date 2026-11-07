"""Verify original current-line import/persistence across independent applications."""
import os
import argparse
from pathlib import Path
import shutil
import tempfile

from settings_process import Application


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--installed-directory", type=Path)
    args = parser.parse_args()
    source = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-current-line-") as name:
        folder = Path(name)
        binary_folder = folder
        if args.installed_directory:
            binary_folder = Path(tempfile.mkdtemp(prefix="current-line-", dir=args.installed_directory))
        executable = binary_folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config_folder = folder
        if args.installed_directory:
            environment["XDG_CONFIG_HOME"] = str(folder / "config")
            config_folder = folder / "config" / "Notepad4"
        config = config_folder / "Notepad4.ini"
        seed = binary_folder / "Notepad4.ini-default"
        seed.write_text("; keep comment\n[Settings]\nSaveSettings=0\nHighlightCurrentLine=11suffix\n"
                        "Unrelated=keep\n[Styles]\nUnrelatedStyle=保留\n", encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["current_line_mode"] == "1" and app.state["current_line_subline"] == "1", app.state
            app.menu("Outline Frame")
            app.menu("Save Settings Now")
            app.menu("No Highlight")
            app.menu("Highlight Subline")
            app.close()
        finally:
            app.stop()
        saved = config.read_text(encoding="utf-16")
        assert "HighlightCurrentLine=" not in saved and "SaveSettings=0" in saved, saved
        assert "Unrelated=keep" in saved and "UnrelatedStyle=保留" in saved and "; keep comment" in saved, saved
        app = Application(executable, folder, environment)
        try:
            assert app.state["current_line_mode"] == "2" and app.state["current_line_subline"] == "1", app.state
            app.menu("Save Settings On Exit")
            app.menu("Background Color")
            app.menu("Highlight Subline")
            app.close()
        finally:
            app.stop()
        assert "HighlightCurrentLine=1" in config.read_text(encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["current_line_mode"] == "1" and app.state["current_line_subline"] == "0", app.state
            app.menu("Save Settings On Exit")
            app.menu("Outline Frame")
            app.close()
        finally:
            app.stop()
        assert "HighlightCurrentLine=1" in config.read_text(encoding="utf-16")
        # Independently captured original normalized settings, including signed
        # remainder, prefix parsing and values with more than one tens digit.
        for value, mode, subline in (("0", "0", "0"), ("1", "1", "0"), ("2", "2", "0"),
                                     ("3", "2", "0"), ("9", "2", "0"), ("10", "0", "1"),
                                     ("11", "1", "1"), ("12", "2", "1"), ("13", "2", "1"),
                                     ("20", "0", "1"), ("21", "1", "1"), ("99", "2", "1"),
                                     ("-1", "0", "0"), ("-12", "0", "0"),
                                     ("11suffix", "1", "1"), ("invalid", "2", "1")):
            config.write_text(f"[Settings]\nSaveSettings=0\nHighlightCurrentLine={value}\n", encoding="utf-16")
            app = Application(executable, folder, environment)
            try:
                assert (app.state["current_line_mode"], app.state["current_line_subline"]) == (mode, subline), (
                    value, app.state)
                app.close()
            finally:
                app.stop()
        if args.installed_directory:
            shutil.rmtree(binary_folder)
    print("PASS: original packed current-line parsing, seed import, reload, exit/explicit save and preserved fields")


if __name__ == "__main__":
    main()
