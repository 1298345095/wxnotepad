"""Compare actual application Save/reload with independently captured original bytes."""
import csv
import os
from pathlib import Path
import shutil
import sys
import tempfile

from special_insert_process import request
from settings_process import Application


def run(source, reference):
    source = source.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-header-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        for row in csv.DictReader(reference.open(encoding="utf-8"), delimiter="\t"):
            encoding = row["encoding"]
            settings = "SaveSettings=0\nWarnLineEndings=0\nLoadASCIIasUTF8=0\nLoadANSIasUTF8=0\nSkipUnicodeDetection=0\n"
            settings += {"utf8": "DefaultEncoding=3", "utf8bom": "DefaultEncoding=4",
                         "utf16": "DefaultEncoding=6", "utf16bom": "DefaultEncoding=1",
                         "ansi": "DefaultEncoding=0", "gbk": "DefaultEncoding=936"}[encoding]
            config.write_text("[Settings]\n" + settings + "\n", encoding="utf-16")
            suffix = {"PlainText": "txt", "Cpp": "cpp", "Python": "py", "Json": "json"}[row["language"]]
            document = folder / f"header-中文😀.{suffix}"
            before = b"A\r\nB\r\nC"
            if encoding.startswith("utf16"):
                before = "A\r\nB\r\nC".encode("utf-16-le")
            if encoding == "utf16bom": before = b"\xff\xfe" + before
            if encoding == "utf8bom": before = b"\xef\xbb\xbf" + before
            document.write_bytes(before)
            app = Application(executable, folder, environment, document)
            try:
                original = dict(app.state)
                app.named_menu("Edit", "Encoding Identifier" if row["command"] == "encoding" else "Script Shebang Line")
                assert app.state["length"] == str(len(bytes.fromhex(row["utf8"]))), row
                assert app.state["dirty"] == "1", row
                for key in ["encoding", "bom", "eol_mode", "zoom"]:
                    assert app.state[key] == original[key], (row, key, app.state, original)
                request(app, "save")
                expected = bytes.fromhex(row["bytes"])
                assert document.read_bytes() == expected, (row, document.read_bytes())
                assert app.state["dirty"] == "0", row
                app.named_menu("Edit", "Undo")
                request(app, "save")
                assert document.read_bytes() == before, row
                app.named_menu("Edit", "Redo")
                request(app, "save")
                assert document.read_bytes() == expected, row
                request(app, "open", document)
                assert app.state["length"] == str(len(bytes.fromhex(row["utf8"]))) and app.state["dirty"] == "0", row
                request(app, "save")
                assert document.read_bytes() == expected, row
                app.close()
            finally:
                app.stop()
    print("PASS: 48 independent header insertion processes retain exact original Save/Undo/Redo/reload bytes")


if __name__ == "__main__":
    run(Path(sys.argv[1]), Path(sys.argv[2]))
