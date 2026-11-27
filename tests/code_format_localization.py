"""Exercise both Web Tools commands in the packaged English and Chinese applications."""

import argparse
import tempfile
from pathlib import Path

from localization_package import menus
from localization_process import PREFERENCE, Acceptance

CAPTIONS = {
    "en": ("&Web Tools", "CSS/JS/JSON &Compress", "CSS/JS/JSON &Pretty"),
    "zh_CN": ("Web开发(&W)", "CSS/JS/JSON 压缩(&C)", "CSS/JS/JSON 美化(&P)"),
    "zh_TW": ("Web開發(&W)", "CSS/JS/JSON 壓縮(&C)", "CSS/JS/JSON 美化(&P)"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake-dir", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    rows = [
        line.split()
        for line in Path(__file__).with_name("code_format_files_reference.tsv").read_text().splitlines()[1:]
    ]
    reference = {bool(int(row[1])): row for row in rows if row[0] == "utf-8" and row[2] == "0"}
    with tempfile.TemporaryDirectory(prefix="code-format-localization-") as directory:
        fixture = Acceptance(args, Path(directory))
        document = Path(directory) / "fixture.json"
        for language, (parent, compress, pretty) in CAPTIONS.items():
            for formatting, caption in [(False, compress), (True, pretty)]:
                row = reference[formatting]
                document.write_bytes(bytes.fromhex(row[3]))
                app = fixture.start(
                    PREFERENCE[language], preferred="en", extra="FileWatchingMode=0\n", startup=document
                )
                try:
                    entries = menus(app.state)
                    parent_id = next(identity for identity, item in entries.items() if item["label"] == parent)
                    path = entries[parent_id]["path"]
                    commands = [
                        next(identity for identity, item in entries.items() if item["label"] == label)
                        for label in [compress, pretty]
                    ]
                    assert [entries[identity]["path"] for identity in commands] == [path + "/0", path + "/1"]
                    assert all(entries[identity]["enabled"] == "1" for identity in commands)
                    app.choose(commands[int(formatting)])
                    assert app.state["dirty"] == "1", (language, caption, app.state)
                    app.send("save")
                    assert document.read_bytes() == bytes.fromhex(row[5]), (language, caption)
                    assert app.state["dirty"] == "0"
                    app.send("open", str(document))
                    assert app.state["dirty"] == "0"
                    app.close()
                finally:
                    app.stop()
    print("PASS: packaged English/Simplified/Traditional Web Tools hierarchy, both commands and original saved bytes")


if __name__ == "__main__":
    main()
