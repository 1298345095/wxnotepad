"""Verify original MatchBraces import/save through independent app processes."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile

from settings_process import Application


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    source = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-visual-brace-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        seed = folder / "Notepad4.ini-default"
        seed.write_text("; keep comment\n[Settings]\nSaveSettings=0\nMatchBraces=0suffix\n"
                        "Unrelated=keep\n[Styles]\nUnrelatedStyle=保留\n", encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["match_braces"] == "0", app.state
            app.menu("Visual Brace Matching")
            assert app.state["match_braces"] == "1", app.state
            app.menu("Save Settings Now")
            app.menu("Visual Brace Matching")
            app.close()
        finally:
            app.stop()
        saved = config.read_text(encoding="utf-16")
        assert "MatchBraces=" not in saved and "SaveSettings=0" in saved, saved
        assert "Unrelated=keep" in saved and "UnrelatedStyle=保留" in saved and "; keep comment" in saved, saved
        app = Application(executable, folder, environment)
        try:
            assert app.state["match_braces"] == "1", app.state
            app.menu("Save Settings On Exit")
            app.menu("Visual Brace Matching")
            app.close()
        finally:
            app.stop()
        assert "MatchBraces=0" in config.read_text(encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert app.state["match_braces"] == "0", app.state
            app.menu("Save Settings On Exit")
            app.menu("Visual Brace Matching")
            app.close()
        finally:
            app.stop()
        assert "MatchBraces=0" in config.read_text(encoding="utf-16")
        # Original Helpers.cpp::GetBoolImpl accepts a leading 0/1; other
        # stored values fall back to Notepad4.cpp's true MatchBraces default.
        for value, expected in (("0", "0"), ("0suffix", "0"), (" 0suffix ", "0"),
                                ("1", "1"), ("1suffix", "1"), ("2", "1"),
                                ("-1", "1"), ("", "1"), ("invalid", "1")):
            config.write_text(f"[Settings]\nSaveSettings=0\nMatchBraces={value}\n", encoding="utf-16")
            app = Application(executable, folder, environment)
            try:
                assert app.state["match_braces"] == expected, (value, app.state)
                app.close()
            finally:
                app.stop()
    print("PASS: original MatchBraces import/default/prefix parsing, explicit/exit saves, restart and retained fields")


if __name__ == "__main__":
    main()
