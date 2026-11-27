"""Named CSV startup and different-file reuse match independently captured originals."""

import argparse
import json
from pathlib import Path
import sys
import tempfile

from csv_process import accept, checked, select_radio
from localization_native import Dialogs
from localization_package import Process, bundle_application, environment, menu_id, settings, wait_child
from resource_process import native_edit, wait_state
from startup_options_process import pending_start


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake-dir", required=True, type=Path)
    parser.add_argument("--forward", action="store_true")
    parser.add_argument("--extension", choices=("txt", "tsv"))
    args = parser.parse_args()
    reference = json.loads(Path(__file__).with_name("startup_csv_reference.json").read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="wxnotepad-startup-csv-") as name:
        root = Path(name).resolve()
        executable = bundle_application(args.executable.resolve(), args.cmake_dir.resolve(), root / "bundle")
        config = executable.parent / "Notepad4.ini"
        config_bytes = settings(config, 1033, "WordWrap=0\n")
        (root / "children").mkdir()
        env = environment(executable, root / "children")
        dialogs = Dialogs(root / "dialogs")
        previous = root / "previous.txt"
        previous.write_bytes(b"previous document\n")
        for index, expected in enumerate(reference["cases"]):
            if args.extension and expected["extension"] != args.extension:
                continue
            target = root / ("semicolon." + expected["extension"])
            original = bytes.fromhex(expected["hex"])
            target.write_bytes(original)
            prefix = "/" if sys.platform == "win32" and index % 2 else "-"
            arguments = (prefix + "s", expected["scheme"], prefix + "g", "1,3", str(target))
            requester = None
            app = Process(executable, root / f"app-{index}", env, startup=previous) if args.forward else None
            try:
                if args.forward:
                    requester = pending_start(
                        executable, root / f"request-{index}", env, (prefix + "r", *arguments), root
                    )
                    assert requester.process.wait(timeout=5) == 0
                    app.send("snapshot")
                else:
                    app = Process(executable, root / f"app-{index}", env, arguments=arguments, cwd=root)
                state = dict(app.state)
                assert Path(state["document_path"]) == target
                assert state["length"] == str(len(original)) and state["caret_position"] == "2"
                assert state["dirty"] == str(expected["modified"])
                assert state["can_undo"] == str(expected["undo"]) and state["can_redo"] == str(expected["redo"])
                assert state["read_only"] == str(expected["readonly"])
                # Observe real lexer output at the second column and native Options.
                app.begin("menu", str(menu_id(state, "CSV File...")))
                window = dialogs.wait(app.process, "CSV Options")
                actual = (
                    int(state["lexer"]),
                    int(state["style_at_caret"]),
                    checked(dialogs, window, "Semicolon (;)"),
                    checked(dialogs, window, "Double quote"),
                )
                accept(dialogs, window)
                app.finish()
                wanted = (
                    expected["lexer"],
                    bytes.fromhex(expected["styles"])[2],
                    expected["semicolon_checked"],
                    expected["double_quote_checked"],
                )
                assert actual == wanted, (args.forward, expected["extension"], expected["scheme"], actual, wanted)
                print(
                    "PASS:",
                    "IPC" if args.forward else "startup",
                    expected["extension"],
                    expected["scheme"],
                    "lexer/style/semicolon/double-quote",
                    actual,
                    flush=True,
                )
                for field in ("caret_position", "selection_start", "selection_end", "dirty", "can_undo", "can_redo"):
                    assert app.state[field] == state[field], (field, state[field], app.state[field])
                if index == 0 and not args.forward:
                    # Original GetRelaunchParameters emits -s for the current CSV
                    # scheme. A child sniffs its disk text; parent options persist.
                    app.begin("menu", str(menu_id(app.state, "CSV File...")))
                    window = dialogs.wait(app.process, "CSV Options")
                    select_radio(dialogs, window, "Pipe (|)")
                    accept(dialogs, window)
                    app.finish()
                    children = root / "children"
                    before = set(path.name for path in children.iterdir()) if children.exists() else set()
                    app.choose(menu_id(app.state, "Open in New Window"))
                    child = wait_child(children, before)
                    try:
                        assert child.process.pid != app.process.pid
                        assert child.state["style_at_caret"] == str(wanted[1])
                        child.begin("menu", str(menu_id(child.state, "CSV File...")))
                        window = dialogs.wait(child.process, "CSV Options")
                        assert checked(dialogs, window, "Semicolon (;)")
                        accept(dialogs, window)
                        child.finish()
                        child.close()
                    finally:
                        child.stop()
                    app.begin("menu", str(menu_id(app.state, "CSV File...")))
                    window = dialogs.wait(app.process, "CSV Options")
                    assert checked(dialogs, window, "Pipe (|)")
                    accept(dialogs, window)
                    app.finish()
                    print("PASS: current-file child sniffs again; parent manual CSV options persist", flush=True)
                # A real edit/Undo/Redo retains ordinary history and exact saved bytes.
                native_edit(app.process, app.state)
                app.state = wait_state(app.folder, app.process, lambda current: current["dirty"] == "1")
                app.choose(menu_id(app.state, "Undo"))
                assert app.state["dirty"] == "0" and app.state["can_redo"] == "1"
                app.choose(menu_id(app.state, "Redo"))
                saved = root / f"saved-{index}.txt"
                app.send("save", str(saved))
                assert saved.read_bytes() == original[:2] + b"X" + original[2:]
                assert target.read_bytes() == original and previous.read_bytes() == b"previous document\n"
                app.close()
            finally:
                if requester:
                    requester.stop()
                if app:
                    app.stop()
            assert config.read_bytes() == config_bytes
    print(
        "PASS: named CSV/TSV schemes sniff .txt/.tsv in real "
        + ("different-file IPC" if args.forward else "startup")
        + ", actual styles/options, history and disk bytes"
    )


if __name__ == "__main__":
    main()
