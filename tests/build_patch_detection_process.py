"""Check original INI content-detection decisions through actual app startup."""

import argparse
import os
import shutil
import tempfile
from pathlib import Path

from localization_package import Process

ORIGINAL_INI_LEXER = 9
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("executable", type=Path)
args = parser.parse_args()
source = args.executable.resolve()
reference = Path(__file__).parent / "data/build_patch_detection_reference.tsv"
with tempfile.TemporaryDirectory(prefix="wxnotepad-ini-detection-") as directory:
    folder = Path(directory)
    executable = folder / source.name
    shutil.copy2(source, executable)
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    (folder / "Notepad4.ini").write_text(
        "[Settings]\nSaveSettings=0\nWordWrap=0\nDetectIndentation=0\nWarnLineEndings=0\n"
        "[Settings2]\nUILanguage=1033\n",
        encoding="utf-16",
    )
    count = 0
    for line in reference.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        name, text, original_lexer = line.split("\t")
        path = folder / name
        raw = bytes.fromhex(text)
        path.write_bytes(raw)
        app = Process(executable, folder / f"case-{count}", environment, path)
        try:
            selected_ini = "Ini Config File" in app.state["document_status"]
            assert selected_ini == (int(original_lexer) == ORIGINAL_INI_LEXER), (
                name,
                original_lexer,
                app.state["document_status"],
            )
            assert app.state["length"] == str(len(raw)) and app.state["dirty"] == "0", app.state
            assert path.read_bytes() == raw
            app.close()
        finally:
            app.stop()
        count += 1
    assert count == 18, count
print("PASS: 18 independent original INI content/association/priority outcomes through actual app startup")
