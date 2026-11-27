"""Original CP932/CP949 settings, localization and Restart in real packaged processes."""

import argparse
import json
from pathlib import Path
import tempfile

from localization_package import Process, bundle_application, environment, menu_id, settings, wait_child


def run(args):
    args.evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="japanese-korean-package-") as name:
        root = Path(name)
        executable = bundle_application(
            args.executable.resolve(), args.cmake_dir.resolve(), root / "bundle", args.evidence / root.name / "bundle"
        )
        config = executable.parent / "Notepad4.ini"
        children = root / "children"
        children.mkdir()
        restart = None
        for code, preference in [("en", 1033), ("zh_CN", 2052), ("zh_TW", 1028)]:
            for page, source, length in [(932, b"\xb6\xfb\xfc\r\n", 8), (949, b"\xb0\xa1\x81\x41\r\n", 8)]:
                settings(config, preference, f"DefaultEncoding={page}\nFileWatchingMode=0\n")
                path = root / f"{code}-cp{page}.txt"
                path.write_bytes(source)
                app = Process(executable, root / f"app-{code}-{page}", environment(executable, children, code), path)
                child = None
                try:
                    if code == "en":
                        restart = menu_id(app.state, "Restart")
                    assert app.state["encoding"] == f"CP{page}" and app.state["length"] == str(length), app.state
                    assert app.state["dirty"] == "0" and app.state["bom"] == "0", app.state
                    before = dict(app.state)
                    previous = {folder.name for folder in children.iterdir()}
                    app.begin("menu", str(restart))
                    assert app.process.wait(timeout=10) == 0
                    child = wait_child(children, previous)
                    assert child.process.pid != app.process.pid
                    for field in ("document_path", "length", "encoding", "bom", "eol_mode", "dirty"):
                        assert child.state[field] == before[field], (field, before, child.state)
                    child.send("save")
                    assert path.read_bytes() == source
                    (args.evidence / root.name / f"{code}-{page}.json").write_text(
                        json.dumps({"parent": before, "child": child.state}, ensure_ascii=False, indent=2),
                        encoding="utf8",
                    )
                    child.close()
                finally:
                    app.stop()
                    if child:
                        child.stop()
                print(
                    f"PASS: packaged {code} CP{page}, numeric default, real Restart identity and exact Save", flush=True
                )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake-dir", required=True, type=Path)
    parser.add_argument("--evidence", required=True, type=Path)
    run(parser.parse_args())
