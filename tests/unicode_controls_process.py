"""Original Unicode display preference through native menus and independent processes."""

import argparse
import os
import shutil
import tempfile
from pathlib import Path

from localization_package import Process, menu_id, menus

TEXT = {
    "en": (1033, "Un&icode Control Character"),
    "zh_CN": (2052, "显示 Unicode 控制字符(&I)"),
    "zh_TW": (1028, "Un&icode 控制字元"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--language", choices=TEXT, default="en")
    args = parser.parse_args()
    source = args.executable.resolve()
    language, caption = TEXT[args.language]
    with tempfile.TemporaryDirectory(prefix="wxnotepad-unicode-controls-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        if (source.parent / "locale").exists():
            shutil.copytree(source.parent / "locale", folder / "locale")
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        source_file = folder / "controls.txt"
        original = ("A\u2060B\u200eC " + "a\u2060b " * 120).encode()
        source_file.write_bytes(original)
        run = 0
        rows = {}

        def seed(save, visible):
            config.write_text(
                f"[Settings]\nSaveSettings={save}\nShowUnicodeControlCharacter={visible}\n"
                "WordWrap=1\nWarnLineEndings=0\nUnrelatedSetting=keep\n"
                f"[Settings2]\nUILanguage={language}\n[Styles]\nUnrelatedStyle=保留\n",
                encoding="utf-16",
            )

        def start(visible):
            nonlocal run
            run += 1
            app = Process(executable, folder / f"run-{run}", environment, source_file)
            try:
                items = menus(app.state)
                found = [identity for identity, item in items.items() if item["label"] == caption]
                assert len(found) == 1, (args.language, caption, items)
                identity = found[0]
                assert items[identity]["checked"] == str(int(visible)) and items[identity]["enabled"] == "1", items[
                    identity
                ]
                if visible in rows:
                    assert int(app.state["wrap_rows"]) == rows[visible], (visible, rows, app.state["wrap_rows"])
            except BaseException:
                app.stop()
                raise
            return app, identity

        seed(0, 1)
        app, identity = start(True)
        try:
            rows[True] = int(app.state["wrap_rows"])
            before = tuple(app.state[key] for key in ("length", "dirty", "encoding", "bom"))
            app.choose(identity)
            assert menus(app.state)[identity]["checked"] == "0"
            rows[False] = int(app.state["wrap_rows"])
            assert rows[True] > rows[False], ("persisted names must change visible wrapping", rows)
            assert tuple(app.state[key] for key in ("length", "dirty", "encoding", "bom")) == before
            save_caption = {"en": "Save Settings Now", "zh_CN": "立即保存设置(N)", "zh_TW": "立即儲存設定(N)"}[
                args.language
            ]
            app.choose(menu_id(app.state, save_caption))
            app.close()
        finally:
            app.stop()
        saved = config.read_text(encoding="utf-16")
        assert "ShowUnicodeControlCharacter=" not in saved and "SaveSettings=0" in saved, saved
        assert "UnrelatedSetting=keep" in saved and "UnrelatedStyle=保留" in saved, saved
        assert source_file.read_bytes() == original
        app, _ = start(False)
        try:
            app.close()
        finally:
            app.stop()

        for save in (0, 1):
            seed(save, 0)
            app, identity = start(False)
            try:
                app.choose(identity)
                assert menus(app.state)[identity]["checked"] == "1"
                assert int(app.state["wrap_rows"]) == rows[True], rows
                app.close()
            finally:
                app.stop()
            saved = config.read_text(encoding="utf-16")
            assert f"ShowUnicodeControlCharacter={save}" in saved, saved
            app, _ = start(bool(save))
            try:
                app.close()
            finally:
                app.stop()
        assert source_file.read_bytes() == original
    print(f"PASS: {args.language} actual localized native menu, INI import, explicit/default/exit save and restart")


if __name__ == "__main__":
    main()
