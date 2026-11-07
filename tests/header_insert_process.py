"""Compare actual application Save/reload with independently captured original bytes."""
import csv
import locale
import os
from pathlib import Path
import shutil
import sys
import tempfile

from special_insert_process import request
from settings_process import Application
from resource_process import command, wait_state


def run(source, reference):
    source = source.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-header-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        rows = list(csv.DictReader(reference.open(encoding="utf-8"), delimiter="\t"))
        utf8_rows = {(row["language"], row["command"]): row for row in rows if row["encoding"] == "utf8"}
        utf8_bom_rows = {(row["language"], row["command"]): row for row in rows if row["encoding"] == "utf8bom"}
        # Linux resolves its native/legacy page from LC_CTYPE. getencoding()
        # ignores Python's -X utf8 mode, independently establishing that context.
        # Windows retains its original ANSI capture context and legacy ACP rules.
        native_utf8 = sys.platform != "win32" and locale.getencoding().lower().replace("-", "") == "utf8"
        native_page = 0
        if sys.platform == "win32":
            import ctypes
            native_page = ctypes.windll.kernel32.GetACP()
        unmigrated_native_ansi = sys.platform == "win32" and native_page not in (936, 65001)
        supported = rejected = 0
        for row in rows:
            encoding = row["encoding"]
            promoted_ansi = native_utf8 and encoding == "ansi"
            expected_row = utf8_rows[(row["language"], row["command"])] if promoted_ansi else row
            rejected_ansi = unmigrated_native_ansi and encoding == "ansi"
            if rejected_ansi:
                expected_row = utf8_bom_rows[(row["language"], row["command"])]
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
            if rejected_ansi:
                rejected_document, rejected_bytes = document, before
                document = folder / f"retained-header-中文😀.{suffix}"
                before = b"\xef\xbb\xbfA\r\nB\r\nC"
                document.write_bytes(before)
            try:
                app = Application(executable, folder, environment, document)
            except Exception as error:
                host = locale.getencoding()
                if sys.platform == "win32":
                    import ctypes
                    host += f" ACP={ctypes.windll.kernel32.GetACP()} OEM={ctypes.windll.kernel32.GetOEMCP()}"
                error.add_note(f"Header startup: {row['language']}/{encoding}/{row['command']}; host={host}")
                raise
            try:
                original = dict(app.state)
                if promoted_ansi:
                    assert original["encoding"] == "UTF-8" and original["bom"] == "0", original
                if rejected_ansi:
                    assert original["encoding"] == "UTF-8" and original["bom"] == "1", original
                app.named_menu("Edit", "Encoding Identifier" if row["command"] == "encoding" else "Script Shebang Line")
                assert app.state["length"] == str(len(bytes.fromhex(expected_row["utf8"]))), row
                assert app.state["dirty"] == "1", row
                for key in ["encoding", "bom", "eol_mode", "zoom"]:
                    assert app.state[key] == original[key], (row, key, app.state, original)
                if rejected_ansi:
                    inserted = dict(app.state)
                    app.sequence += 1
                    command(folder, f"{app.sequence}\nopen\n{rejected_document}\n")
                    app.state = wait_state(folder, app.process, lambda state: state["sequence"] == str(app.sequence))
                    assert app.state["result"] == "failed", (row, native_page, app.state)
                    for key in ["length", "dirty", "encoding", "bom", "eol_mode", "zoom"]:
                        assert app.state[key] == inserted[key], (row, key, app.state, inserted)
                    assert rejected_document.read_bytes() == rejected_bytes, row
                    rejected += 1
                else:
                    supported += 1
                request(app, "save")
                expected = bytes.fromhex(expected_row["bytes"])
                assert document.read_bytes() == expected, (row, document.read_bytes())
                assert app.state["dirty"] == "0", row
                app.named_menu("Edit", "Undo")
                request(app, "save")
                assert document.read_bytes() == before, row
                app.named_menu("Edit", "Redo")
                request(app, "save")
                assert document.read_bytes() == expected, row
                request(app, "open", document)
                assert app.state["length"] == str(len(bytes.fromhex(expected_row["utf8"]))) and app.state["dirty"] == "0", row
                request(app, "save")
                assert document.read_bytes() == expected, row
                app.close()
            finally:
                app.stop()
        assert supported + rejected == 48 and rejected == (8 if unmigrated_native_ansi else 0)
    print(f"PASS: {supported} supported header contexts; {rejected} native ANSI Open refusal contexts "
          f"(ACP={native_page}); 48 independent processes retain original Save/Undo/Redo/reload bytes")


if __name__ == "__main__":
    run(Path(sys.argv[1]), Path(sys.argv[2]))
