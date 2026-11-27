"""Guarded installed Favorites check for the coordinator's isolated Windows VM.

Never registered in the host desktop suite: this requires an explicitly named
VM and an absent real per-user Notepad4 INI, which is restored to absent.
"""

import argparse
import os
import shutil
import sys
import tempfile
import uuid
from contextlib import ExitStack
from pathlib import Path

from file_favorites_packaged import accept_dialog, select_favorite, shortcut
from localization_native import Dialogs
from localization_package import Process, environment, menu_id, settings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--owned-root", type=Path, required=True)
    parser.add_argument("--vm-computer-name", required=True)
    args = parser.parse_args()
    assert sys.platform == "win32"
    assert os.environ["COMPUTERNAME"].casefold() == args.vm_computer_name.casefold(), "Explicit isolated VM only"
    local = Path(os.environ["LOCALAPPDATA"]).resolve()
    root = args.owned_root.resolve()
    assert root.is_relative_to(local) and root.name.startswith("WxNotepadWindowTools288-")
    root.mkdir(parents=True, exist_ok=True)
    args.evidence.mkdir(parents=True, exist_ok=True)
    config = local / "Notepad4" / "Notepad4.ini"
    assert not config.exists(), "Installed-mode test requires an absent real per-user Notepad4 INI"
    directory_existed = config.parent.exists()
    owner = str(uuid.uuid4())
    app = None
    dialogs = Dialogs(args.evidence)
    try:
        with ExitStack() as cleanup:
            name = cleanup.enter_context(tempfile.TemporaryDirectory(prefix="installed-run-", dir=root))
            runtime = Path(name)
            package = runtime / "WinGet"
            shutil.copytree(args.package, package, ignore=shutil.ignore_patterns("Notepad4.ini"))
            executable = package / "wxnotepad.exe"
            assert executable.is_file()
            favorites = package / "favorites"
            favorites.mkdir(exist_ok=True)
            target = runtime / "installed 文档.txt"
            target.write_bytes("installed favorite 内容\n".encode())
            other = runtime / "other.txt"
            other.write_bytes(b"other document\n")
            config.parent.mkdir(parents=True, exist_ok=True)
            settings(config, 1033, f"Favorites=favorites\nFavoritesFixtureOwner={owner}\n", save=True)
            env = environment(executable, runtime / "children", "en")
            for attempt in range(2):
                app = Process(
                    executable,
                    runtime / f"observe-{attempt}",
                    env,
                    startup=other,
                    log_path=args.evidence / f"installed-{attempt}.log",
                )
                cleanup.callback(app.stop)
                if attempt == 0:
                    app.send("open", str(target))
                    app.begin("menu", str(menu_id(app.state, "Add Current File...")))
                    window = dialogs.wait(app.process, "Add to Favorites")
                    dialogs.set_input(window, "Favorite name", "installed favorite")
                    accept_dialog(dialogs, window)
                    response = dialogs.wait(app.process, "", message="The favorite was added successfully.")
                    dialogs.answer(app.process, "ok", window=response)
                    app.finish()
                    assert Path(shortcut(favorites / "installed favorite.lnk")) == target
                    app.send("open", str(other))
                app.begin("menu", str(menu_id(app.state, "Open Favorites...")))
                window = dialogs.wait(app.process, "Favorites")
                select_favorite(dialogs, app, window)
                app.finish()
                assert Path(app.state["document_path"]) == target
                copy = runtime / f"loaded-{attempt}.txt"
                app.send("save", str(copy))
                assert copy.read_bytes() == target.read_bytes()
                app.close()
                app.stop()
                app = None
                assert not (package / "Notepad4.ini").exists(), "Installed mode never writes executable-local INI"
                saved = config.read_text(encoding="utf-16")
                assert "Favorites=" in saved and "UnrelatedSetting=keep" in saved and owner in saved
                (args.evidence / f"installed-{attempt}.ini.txt").write_text(saved, encoding="utf-8")
            (args.evidence / "installed-result.txt").write_text(
                f"computer={os.environ['COMPUTERNAME']}\nconfig={config}\n"
                "native_shortcut_target_and_document_bytes=passed\nrestart=passed\n",
                encoding="utf-8",
            )
    finally:
        if app is not None:
            app.stop()
        if config.exists():
            assert owner in config.read_text(encoding="utf-16"), "Cleanup only this fixture's guarded configuration"
            config.unlink()
        if not directory_existed and config.parent.exists():
            config.parent.rmdir()
        assert not config.exists(), "Restore initial absent per-user INI"
        (args.evidence / "installed-cleanup.txt").write_text("config_exists=false\n", encoding="utf-8")
    print("PASS: isolated Windows installed Favorites, native .lnk, existing file load and per-user INI restart")


if __name__ == "__main__":
    main()
