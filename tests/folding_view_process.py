"""Import and persist original folding preferences in independent real applications."""
import os
from pathlib import Path
import shutil
import sys
import tempfile

from settings_process import Application


def preferences(app, enabled):
    assert (app.state["code_folding"], app.state["current_block"]) == (str(int(enabled)),) * 2, app.state
    assert (int(app.state["fold_margin"]) > 0) == enabled, app.state


source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="wxnotepad-folding-view-") as name:
    folder = Path(name)
    executable = folder / source.name
    shutil.copy2(source, executable)
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    config = folder / "Notepad4.ini"
    seed = folder / "Notepad4.ini-default"
    seed.write_text("; retained comment\n[Settings]\nSaveSettings=0\nShowCodeFolding=0suffix\n"
                    "HighlightCurrentBlock=0suffix\nUnrelated=keep\n[Custom]\nValue=preserved\n", encoding="utf-16")
    app = Application(executable, folder, environment)
    try:
        preferences(app, False)
        app.named_menu("Language", "JSON")
        preferences(app, False)
        for item in ("Show Code Folding", "Highlight Current Block"):
            app.menu(item)
        preferences(app, True)
        app.menu("Save Settings Now")
        for item in ("Show Code Folding", "Highlight Current Block"):
            app.menu(item)
        app.close()
    finally:
        app.stop()
    saved = config.read_text(encoding="utf-16")
    assert "ShowCodeFolding=" not in saved and "HighlightCurrentBlock=" not in saved, saved
    assert all(value in saved for value in ("SaveSettings=0", "Unrelated=keep", "Value=preserved", "; retained comment")), saved
    app = Application(executable, folder, environment)
    try:
        preferences(app, True)
        app.menu("Save Settings On Exit")
        for item in ("Show Code Folding", "Highlight Current Block"):
            app.menu(item)
        app.named_menu("Language", "C/C++")
        app.named_menu("File", "New")
        preferences(app, False)
        app.close()
    finally:
        app.stop()
    saved = config.read_text(encoding="utf-16")
    assert "ShowCodeFolding=0" in saved and "HighlightCurrentBlock=0" in saved, saved
    startup = folder / "startup.py"
    startup.write_bytes(b"class Example:\n    pass\n")
    app = Application(executable, folder, environment, startup)
    try:
        preferences(app, False)
        app.menu("Save Settings On Exit")
        for item in ("Show Code Folding", "Highlight Current Block"):
            app.menu(item)
        app.close()
    finally:
        app.stop()
    assert "ShowCodeFolding=0" in config.read_text(encoding="utf-16")
    for value, expected in ((None, True), ("0", False), ("1", True), ("2", True), ("-1", True),
                            ("invalid", True), ("0suffix", False), ("1suffix", True)):
        fields = "" if value is None else f"ShowCodeFolding={value}\nHighlightCurrentBlock={value}\n"
        config.write_text("[Settings]\nSaveSettings=0\n" + fields, encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            preferences(app, expected)
            app.close()
        finally:
            app.stop()
print("PASS: original folding defaults, prefix parsing, seed import, explicit/exit save, restart and retained INI fields")
