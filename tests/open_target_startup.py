"""Packaged production startup verifies target actions and all released captions."""

import argparse
import tempfile
from pathlib import Path

from localization_package import Process, bundle_application, environment, menus, settings, wait_child

CAPTIONS = {
    1033: ("en", "Action on &Selection", "&Open File, Folder, Link, etc.", "Open Containing &Folder"),
    2052: ("zh_CN", "对选中内容执行操作(&S)", "打开文件、文件夹、链接等(&O)", "打开所在文件夹(&F)"),
    1028: ("zh_TW", "對選取內容執行動作(&S)", "開啟檔案、資料夾、連結等(&O)", "開啟所在資料夾(&F)"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake-dir", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-target-startup-") as name:
        # Match native Document Open and Explorer's expansion of TEMP's 8.3 aliases.
        root = Path(name).resolve(strict=True)
        executable = bundle_application(args.executable.resolve(), args.cmake_dir.resolve(), root / "bundle")
        target = root / "目标.txt"
        target_bytes = "first\nabcd中文\nlast\n".encode("utf-8")
        target.write_bytes(target_bytes)
        source = root / "source.txt"
        source_bytes = (str(target) + ":2:6").encode("utf-8")
        source.write_bytes(source_bytes)
        for language, (code, selection, opening, folder) in CAPTIONS.items():
            settings(executable.parent / "Notepad4.ini", language)
            children = root / code / "children"
            children.mkdir(parents=True)
            app = Process(executable, root / code / "parent", environment(executable, children, code), source, cwd=root)
            child = None
            try:
                entries = menus(app.state)
                action = next(identity for identity, entry in entries.items() if entry["label"] == opening)
                assert any(entry["label"] == selection for entry in entries.values()), entries
                folders = [entry for entry in entries.values() if entry["label"] == folder]
                assert len(folders) == 2 and all(entry["enabled"] == "1" for entry in folders), folders
                assert not list(children.iterdir()), "target detection/menu observation must not launch a child"
                before = {
                    key: app.state[key]
                    for key in (
                        "document_path",
                        "length",
                        "dirty",
                        "caret_position",
                        "selection_start",
                        "selection_end",
                        "encoding",
                    )
                }
                app.choose(action)
                child = wait_child(children)
                assert child.process.pid != app.process.pid, "target must open in a separate process"
                assert Path(child.state["document_path"]) == target, child.state
                assert child.state["caret_position"] == "13", child.state
                assert child.state["dirty"] == "0" and child.state["encoding"] == "UTF-8", child.state
                child.send("save")
                assert target.read_bytes() == target_bytes, "independently saved child retains exact source bytes"
                assert {key: app.state[key] for key in before} == before, app.state
                assert source.read_bytes() == source_bytes
                child.close()
                child = None
                app.choose(5002)  # Stock File/New; the named source is unmodified.
                entries = menus(app.state)
                assert entries[action]["enabled"] == "0", entries[action]
                assert all(entry["enabled"] == "0" for entry in entries.values() if entry["label"] == folder)
                app.close()
                print(f"PASS: {code} packaged native captions and independent production startup file/line/column")
            finally:
                if child:
                    child.stop()
                app.stop()


if __name__ == "__main__":
    main()
