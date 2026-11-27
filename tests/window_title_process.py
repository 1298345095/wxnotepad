"""Window-title preferences through the shown application and real INI startup."""

import argparse
import os
from pathlib import Path
import shutil
import tempfile
from contextlib import ExitStack
from localization_package import Process, menus, menu_id, read_settings, wait_child


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    source = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-title-") as name, ExitStack() as processes:
        folder = Path(name).resolve()
        executable = folder / source.name
        shutil.copy2(source, executable)
        if (source.parent / "locale").exists():
            shutil.copytree(source.parent / "locale", folder / "locale")
        config = folder / "Notepad4.ini"
        path = folder / "named 中文.txt"
        path.write_text("captured text", encoding="utf-8")
        children = folder / "children"
        children.mkdir()
        environment = dict(
            os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"], WXNOTEPAD_OBSERVE_ROOT=str(children)
        )
        run = 0

        def seed(save, mode):
            preference = "" if mode is None else f"PathNameFormat={mode}\n"
            config.write_text(
                f"[Settings]\nSaveSettings={save}\nWarnLineEndings=0\n{preference}"
                "UnrelatedSetting=keep\n[Settings2]\nUILanguage=1033\n"
                "[Unrelated]\nStyle=保留\n",
                encoding="utf-16",
            )

        def title(mode):
            return (
                path.name if mode == 0 else f"{path.name} [{path.parent}]" if mode == 1 else str(path)
            ) + " - wxnotepad"

        def commands(app):
            items = menus(app.state)
            parent = next(item["path"] for item in items.values() if item["label"] == "Window Title Displa&y")
            children = sorted(
                (item["path"], identity) for identity, item in items.items() if item["path"].startswith(parent + "/")
            )
            assert [items[identity]["label"] for _, identity in children] == [
                "&File Name Only",
                "File Name and &Directory",
                "Full &Path Name",
                "&Text Excerpt",
            ]
            return [identity for _, identity in children]

        def start(mode):
            nonlocal run
            run += 1
            app = Process(executable, folder / f"run-{run}", environment, path)
            processes.callback(app.stop)
            assert app.state["title"] == title(mode), app.state["title"]
            assert [menus(app.state)[identity]["checked"] for identity in commands(app)] == [
                str(int(index == mode)) for index in range(4)
            ]
            return app

        def finish(app):
            try:
                app.close()
            finally:
                app.stop()

        def saved(mode, saving):
            data = read_settings(config)
            assert data.getint("Settings", "PathNameFormat", fallback=1) == mode
            assert data.getboolean("Settings", "SaveSettings", fallback=True) == bool(saving)
            assert data["Settings"]["UnrelatedSetting"] == "keep" and data["Unrelated"]["Style"] == "保留"
            assert "captured" not in config.read_text(encoding="utf-16")

        for imported, expected in [(None, 1), (-5, 0), (0, 0), (1, 1), (2, 2), (99, 2)]:
            seed(0, imported)
            finish(start(expected))
            data = read_settings(config)
            assert data.getint("Settings", "PathNameFormat", fallback=1) == (1 if imported is None else imported)
        for save in (0, 1):
            for mode in range(3):
                seed(save, 2)
                app = start(2)
                before = tuple(app.state[key] for key in ("length", "dirty", "encoding", "bom", "document_path"))
                app.choose(commands(app)[mode])
                assert app.state["title"] == title(mode)
                assert (
                    tuple(app.state[key] for key in ("length", "dirty", "encoding", "bom", "document_path")) == before
                )
                finish(app)
                persisted = mode if save else 2
                saved(persisted, save)
                if save and mode == 1:
                    assert "PathNameFormat=" not in config.read_text(encoding="utf-16")
                finish(start(persisted))
        for mode in range(3):
            seed(0, 2)
            app = start(2)
            app.choose(commands(app)[mode])
            app.choose(menu_id(app.state, "Save Settings Now"))
            finish(app)
            saved(mode, 0)
            finish(start(mode))
        for initial_save in (0, 1):
            seed(initial_save, 2)
            app = start(2)
            app.choose(commands(app)[0])
            app.choose(menu_id(app.state, "Save Settings On Exit"))
            finish(app)
            saved(2 if initial_save else 0, 1 - initial_save)

        seed(1, 2)
        app = start(2)
        try:
            app.choose(commands(app)[0])
            app.choose(
                next(
                    identity
                    for identity, item in menus(app.state).items()
                    if item["path"].startswith("1/") and item["label"].split("\t")[0].replace("&", "") == "Select All"
                )
            )
            app.choose(commands(app)[3])
            assert app.state["title"] == '"captured text" - wxnotepad'
            previous = {item.name for item in children.iterdir()}
            app.choose(menu_id(app.state, "New Window"))
            child = wait_child(children, previous)
            try:
                assert child.state["title"] == "Untitled - wxnotepad"
                assert menus(child.state)[commands(child)[2]]["checked"] == "1"
                child.close()
            finally:
                child.stop()
            previous = {item.name for item in children.iterdir()}
            app.begin("menu", str(menu_id(app.state, "Restart")))
            child = wait_child(children, previous)
            try:
                assert child.state["title"] == title(0), child.state["title"]
                assert child.state["document_path"] == str(path)
                child.close()
            finally:
                child.stop()
            assert app.process.wait(timeout=15) == 0
            saved(0, 1)
        finally:
            app.stop()
        assert path.read_text(encoding="utf-8") == "captured text"
    print("PASS: PathNameFormat import/clamp/default/save flags, transient excerpt and independent New Window/Restart")


if __name__ == "__main__":
    main()
