"""Caret preferences through independent real application processes and saved INI bytes."""
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
    with tempfile.TemporaryDirectory(prefix="wxnotepad-caret-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        seed = folder / "Notepad4.ini-default"
        seed.write_text("; keep comment\n[Settings]\nSaveSettings=0\nCaretStyle=113suffix\n"
                        "CaretBlinkPeriod=275suffix\nUnrelated=keep\n[Styles]\nUnrelatedStyle=保留\n", encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            assert (app.state["caret_style"], app.state["caret_period"]) == ("273", "275"), app.state
            app.menu("No Blink")
            assert app.state["caret_period"] == "0", app.state
            app.menu("Save Settings Now")
            app.menu("Line Width 1")
            app.close()
        finally:
            app.stop()
        saved = config.read_bytes().decode("utf-16")
        assert "CaretStyle=113" in saved and "CaretBlinkPeriod=0" in saved and "SaveSettings=0" in saved, saved
        assert "; keep comment" in saved and "Unrelated=keep" in saved and "UnrelatedStyle=保留" in saved, saved
        app = Application(executable, folder, environment)
        try:
            assert (app.state["caret_style"], app.state["caret_period"]) == ("273", "0"), app.state
            app.menu("Draw Block Caret in Selection")
            app.menu("Block (OVR Mode)")
            app.menu("Line Width 1")
            app.menu("No Blink")
            system_period = app.state["caret_period"]
            app.menu("Save Settings On Exit")
            app.close()
        finally:
            app.stop()
        saved = config.read_bytes().decode("utf-16")
        assert "CaretStyle=" not in saved and "CaretBlinkPeriod=" not in saved, saved
        fixtures = Path(__file__).with_name("caret_reference.tsv").read_text(encoding="utf-8")
        for line in fixtures.splitlines():
            if not line or line.startswith("#"):
                continue
            packed, blink, style, normalized = map(int, line.split("\t"))
            config.write_text(f"[Settings]\nCaretStyle={packed}\nCaretBlinkPeriod={blink}\n", encoding="utf-16")
            app = Application(executable, folder, environment)
            try:
                assert app.state["caret_style"] == str(style), (line, app.state)
                assert app.state["caret_period"] == (system_period if blink < 0 else str(blink)), (line, app.state)
                app.menu("Save Settings Now")
                initial_saved = config.read_bytes().decode("utf-16")
                assert ("CaretBlinkPeriod=" not in initial_saved if blink == -1 else
                        f"CaretBlinkPeriod={blink}\r\n" in initial_saved), initial_saved
                app.menu("No Blink")
                assert app.state["caret_period"] == (system_period if blink == 0 else "0"), (line, app.state)
                app.close()
            finally:
                app.stop()
            saved = config.read_bytes().decode("utf-16")
            assert ("CaretStyle=" not in saved if normalized == 1 else f"CaretStyle={normalized}\r\n" in saved), saved
            assert ("CaretBlinkPeriod=" not in saved if blink == 0 else "CaretBlinkPeriod=0\r\n" in saved), saved
        for packed, blink in [("invalid", "invalid"), ("1suffix", "-7suffix")]:
            config.write_text(f"[Settings]\nCaretStyle={packed}\nCaretBlinkPeriod={blink}\n", encoding="utf-16")
            app = Application(executable, folder, environment)
            try:
                assert (app.state["caret_style"], app.state["caret_period"]) == ("1", system_period), app.state
                app.close()
            finally:
                app.stop()
    print("PASS: original packed caret fixtures, system/custom/zero periods, seed, independent restart and INI persistence")


if __name__ == "__main__":
    main()
