"""Original scheme identities/default/detection through the actual shown application."""

import argparse
import os
from pathlib import Path
import shutil
import tempfile

from localization_package import Process, menu_id, menus


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    source = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-scheme-process-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        if (source.parent / "locale").exists():
            shutil.copytree(source.parent / "locale", folder / "locale")
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        unknown = folder / "input.unknown"
        cpp = folder / "input.cpp"
        unknown.write_bytes(b"data\n")
        cpp.write_bytes(b"int value;\n")
        run = 0
        for identity, language in ((32, "Python"), (54, "JSON"), (5, "Plain Text")):
            for automatic in (0, 1):
                for save in (0, 1):
                    config.write_text(
                        f"[Settings]\nSaveSettings={save}\nWarnLineEndings=0\n"
                        "UnrelatedSetting=keep\n[Settings2]\nUILanguage=1033\n"
                        f"[Styles]\nDefaultScheme={identity}\nAutoSelect={automatic}\n"
                        "FavoriteSchemes=999 67 1\nUnrelatedStyle=保留\n",
                        encoding="utf-16",
                    )
                    for restart in range(2):
                        run += 1
                        app = Process(executable, folder / f"run-{run}", environment, unknown)
                        try:
                            assert language in app.state["document_status"], (identity, app.state)
                            assert menus(app.state)[menu_id(app.state, "Automatic")]["checked"] == "1"
                            if identity == 54:
                                assert (app.state["tab_width"], app.state["use_tabs"]) == ("2", "0"), app.state
                            if restart == 0:
                                app.choose(menu_id(app.state, "C/C++"))
                                assert menus(app.state)[menu_id(app.state, "C/C++")]["checked"] == "1"
                                assert "C/C++ (override)" in app.state["document_status"], app.state
                                renamed = folder / "saved.unknown"
                                app.send("save", str(renamed))
                                assert renamed.read_bytes() == unknown.read_bytes() == b"data\n"
                                app.choose(menu_id(app.state, "Revert"))
                                assert "C/C++ (override)" in app.state["document_status"], app.state
                                assert app.state["dirty"] == "0" and app.state["length"] == "5", app.state
                                app.send("open", str(cpp))
                                expected = "C/C++" if automatic else language
                                assert expected in app.state["document_status"], (automatic, app.state)
                                assert menus(app.state)[menu_id(app.state, "Automatic")]["checked"] == "1"
                                app.choose(menu_id(app.state, "New"))
                                assert language in app.state["document_status"] and app.state["length"] == "0"
                                assert app.state["dirty"] == "0" and app.state["document_path"] == ""
                                app.choose(menu_id(app.state, "Save Settings Now"))
                            app.close()
                        finally:
                            app.stop()
                        saved = config.read_text(encoding="utf-16")
                        assert f"DefaultScheme={identity}" in saved, saved
                        assert ("AutoSelect=0" in saved) == (automatic == 0), saved
                        assert "FavoriteSchemes=999 67 1" in saved and "UnrelatedStyle=保留" in saved, saved
                        assert "UnrelatedSetting=keep" in saved and ("SaveSettings=0" in saved) == (save == 0), saved
        assert unknown.read_bytes() == b"data\n" and cpp.read_bytes() == b"int value;\n"
    print("PASS: original stable IDs/default/favorites, New/Open/Save/Reload, menu checks and settings restart")


if __name__ == "__main__":
    main()
