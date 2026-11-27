"""Verify calculation through the real packaged application's native menus."""

import argparse
import hashlib
import json
import shutil
import tempfile
import time
from pathlib import Path

from localization_package import Process, bundle_application, environment, menus, settings

CAPTIONS = {1033: "&Calculate Expression", 2052: "计算表达式(&C)", 1028: "計算運算式(&C)"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    args.evidence = args.evidence / f"run-{time.time_ns()}"
    args.evidence.mkdir(parents=True, exist_ok=False)
    samples = [
        tuple(map(json.loads, row.split("\t")))
        for row in Path(__file__)
        .with_name("calculate_expression_reference.tsv")
        .read_text(encoding="utf-8")
        .splitlines()
    ]
    with tempfile.TemporaryDirectory(prefix="wxnotepad-calculation-package-") as temporary:
        root = Path(temporary)
        executable = bundle_application(
            args.executable.resolve(), args.cmake.resolve(), root / "bundle", args.evidence / "bundle"
        )
        for language, caption in CAPTIONS.items():
            settings(executable.parent / "Notepad4.ini", language)
            document = root / "expression.txt"
            document.write_bytes(b"")
            app = Process(
                executable,
                args.evidence / str(language),
                environment(executable, root / "children"),
                startup=document,
                cwd=root,
            )
            records = []
            try:
                observed = menus(app.state)
                calculate = [identity for identity, item in observed.items() if item["label"] == caption]
                assert len(calculate) == 1, "Original Calculate Expression command exists in packaged application"
                calculate = calculate[0]
                assert observed[calculate]["enabled"] == "0", "Empty selection disables calculation"
                assert "\t" not in observed[calculate]["label"], "Original calculation has no invented shortcut"
                select_all = next(identity for identity, item in observed.items() if item["label"].endswith("\tCtrl+A"))
                undo = next(identity for identity, item in observed.items() if item["label"].endswith("\tCtrl+Z"))
                redo = next(identity for identity, item in observed.items() if item["label"].endswith("\tCtrl+Y"))
                for index, (expression, inserted) in enumerate(samples):
                    # Reopen a fresh real file through the actual application;
                    # the fixture never substitutes an editor/evaluator backend.
                    source = root / f"expression-{index}.txt"
                    source.write_bytes(expression.encode())
                    app.send("open", str(source))
                    app.choose(select_all)
                    before = dict(app.state)
                    app.choose(calculate)
                    after = dict(app.state)
                    expected = (expression + inserted).encode()
                    if inserted:
                        assert (after["selection_start"], after["selection_end"], after["caret_position"]) == (
                            str(len(expression.encode())),
                            str(len(expected)),
                            str(len(expected)),
                        ), (expression, after)
                        assert after["dirty"] == "1" and after["can_undo"] == "1", (expression, after)
                    else:
                        for field in (
                            "selection_start",
                            "selection_end",
                            "caret_position",
                            "dirty",
                            "can_undo",
                            "can_redo",
                        ):
                            assert after[field] == before[field], (expression, field, before, after)
                    app.send("save")
                    assert source.read_bytes() == expected, (language, expression, source.read_bytes(), expected)
                    if inserted:
                        app.choose(undo)
                        assert app.state["dirty"] == "1" and app.state["can_redo"] == "1", app.state
                        app.choose(redo)
                        assert app.state["dirty"] == "0", "Redo returns to actual saved result save point"
                    records.append(
                        dict(
                            expression=expression,
                            inserted=inserted,
                            before=before,
                            after=after,
                            saved_hex=source.read_bytes().hex(),
                        )
                    )
                (args.evidence / f"{language}-results.json").write_text(
                    json.dumps(records, ensure_ascii=False, indent=2), encoding="utf-8"
                )
                app.close()
            finally:
                app.stop()
        notice = executable.parent / "licenses/vcpkg/quickjs-ng/copyright"
        if executable.suffix != ".exe":
            assert notice.is_file(), "Packaged static engine keeps complete dependency notices"
            shutil.copy2(notice, args.evidence / "quickjs-ng-copyright.txt")
            for record in ("vcpkg.spdx.json", "vcpkg-spdx-resources.json"):
                assert notice.with_name(record).is_file(), "Packaged engine keeps actual dependency package records"
            build_record = json.loads((executable.parent / "build.json").read_text(encoding="utf-8"))
            engine = dict(build_record["expression_engine"])
            digest = engine.pop("package_record_sha256")
            assert engine == {
                "name": "quickjs-ng",
                "version": "0.16.2",
                "license": "MIT AND Unicode-3.0",
                "linkage": "static",
            }, build_record
            assert digest == hashlib.sha256(notice.with_name("vcpkg.spdx.json").read_bytes()).hexdigest()
    print("PASS: packaged English/Simplified/Traditional expressions, result selection, saved bytes and Undo/Redo")


if __name__ == "__main__":
    main()
