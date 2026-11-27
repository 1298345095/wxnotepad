"""Verify original detection settings through actual independent application processes."""

import argparse
import os
import shutil
import tempfile
from pathlib import Path

from english_settings import english_settings
from resource_process import command, wait_state
from settings_process import Application


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    source = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-shebang-settings-") as temporary:
        folder = Path(temporary)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        startup = folder / "script"
        original = b"#!/usr/bin/env python3\r\nreturn 42\n"
        startup.write_bytes(original)
        # Pinned original observation: NoCGIGuess disables interpreter sniffing,
        # while .cgi still selects the original Perl association now delivered.
        pinned_python_lexer, pinned_perl_lexer = "2", "6"
        cases = (
            ("", "Python", "Python"),
            ("[Styles]\nAutoSelect=0\n", "Plain Text", "Plain Text"),
            ("[Settings2]\nNoCGIGuess=1\n", "Plain Text", "Perl Script"),
            ("[Settings2]\nNoAutoDetection=1\n", "Python", "Python"),
            ("[Styles]\nAutoSelect=invalid\n[Settings2]\nNoCGIGuess=invalid\n", "Python", "Python"),
            ("[Styles]\nAutoSelect=1suffix\n[Settings2]\nNoCGIGuess=0suffix\n", "Python", "Python"),
            ("[Settings]\nAutoSelect=0\nNoCGIGuess=1\n", "Python", "Python"),
        )
        for settings, expected, cgi_expected in cases:
            config.write_text(
                english_settings("[Settings]\nSaveSettings=0\nWarnLineEndings=0\n" + settings), encoding="utf-16"
            )
            app = Application(executable, folder, environment, startup)
            try:
                assert app.state["document_status"] == f"UTF-8 | {expected} | EOL: CRLF", app.state
                for item, result in (("Python", "Python (override)"), ("Automatic", expected)):
                    app.named_menu("Scheme", item)
                    assert app.state["document_status"] == f"UTF-8 | {result} | EOL: CRLF", app.state
                for suffix, language in (
                    (".py", "Plain Text" if "[Styles]\nAutoSelect=0" in settings else "Python"),
                    (".cgi", cgi_expected),
                ):
                    candidate = folder / ("script" + suffix)
                    candidate.write_bytes(original)
                    app.sequence += 1
                    command(folder, f"{app.sequence}\nopen\n{candidate}\n")
                    app.state = wait_state(folder, app.process, lambda s: s["sequence"] == str(app.sequence))
                    assert app.state["result"] == "ok", app.state
                    assert app.state["document_status"] == f"UTF-8 | {language} | EOL: CRLF", app.state
                    if suffix == ".cgi" and cgi_expected == "Perl Script":
                        assert app.state["lexer"] != pinned_python_lexer, "NoCGIGuess suppresses Python sniffing"
                        assert app.state["lexer"] == pinned_perl_lexer, "original .cgi association selects Perl"
                    assert candidate.read_bytes() == original
                app.menu("Save Settings Now")
                app.close()
            finally:
                app.stop()
            assert startup.read_bytes() == original
            saved = config.read_text(encoding="utf-16")
            if expected == "Plain Text" and "AutoSelect=0" in settings:
                assert "AutoSelect=0" in saved
            if "NoCGIGuess=" in settings:
                assert "NoCGIGuess=" + settings.split("NoCGIGuess=", 1)[1].splitlines()[0] in saved
            print(f"PASS: original settings {settings!r} -> {expected}")


if __name__ == "__main__":
    main()
